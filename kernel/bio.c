// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// 哈希表的桶号索引.设置个数为质数的桶数可以降低哈希冲突
#define NBUFMAP_BUCKET 13
// 哈希桶索引计算 (key 值)
#define BUFMAP_HASH(dev, blockno) ((((dev)<<27) | (blockno)) % NBUFMAP_BUCKET)

struct {
  // struct spinlock lock;
  struct buf buf[NBUF];
  
  // 将双链表改为哈希表
  struct buf bufmap[NBUFMAP_BUCKET];
  struct spinlock bufmap_locks[NBUFMAP_BUCKET];

  // 添加驱逐锁
  struct spinlock eviction_lock;

} bcache;

void
binit(void)
{
  // 初始化桶锁
  for(int i = 0; i < NBUFMAP_BUCKET; i++){
    initlock(&bcache.bufmap_locks[i], "bcache_bufmap");
    bcache.bufmap[i].next = 0;
  }

  // 初始化缓冲区块，初始时均连接到桶号索引为 0 的哈希桶中
  for (int i = 0; i < NBUF; i++)
  {
    struct buf* b = &bcache.buf[i];
    initsleeplock(&b->lock,"buffer");
    b->lastuse = 0;
    b->refcnt = 0;
    
    // 所有缓冲区块添加到哈希缓冲 bufmap[0] 中
    b->next = bcache.bufmap[0].next;
    bcache.bufmap[0].next = b;
  }
  initlock(&bcache.eviction_lock, "bcache_eviction");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
// 从缓冲区缓存中获取磁盘块.
// 若该磁盘块已经在缓存中,就增加其引用计数并返回;若没有缓存该块,则选择一个空闲的缓冲区与指定的磁盘块关联
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // 哈希计算获取桶号
  uint key = BUFMAP_HASH(dev, blockno);

  // 哈希桶上锁
  acquire(&bcache.bufmap_locks[key]);

  // 查找缓冲区块
  // 若在缓冲区中
  for(b = bcache.bufmap[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt ++;
      release(&bcache.bufmap_locks[key]);
      acquiresleep(&b->lock);     // 表示其他线程不能再修改这个缓冲区了
      return b;
    }
  }

  // 若缓冲区未命中,则先释放哈希桶锁,再获取驱逐锁
  release(&bcache.bufmap_locks[key]);
  acquire(&bcache.eviction_lock);     // 查找替换的时候上锁

  // 在释放桶锁到获取驱逐锁的间隙可能会有别的线程创建了需要的缓冲区块,所以要再检查一次,避免重复创建缓冲区块
  for(b = bcache.bufmap[key].next; b; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      acquire(&bcache.bufmap_locks[key]);
      b->refcnt++;
      release(&bcache.bufmap_locks[key]);
      release(&bcache.eviction_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 如果在这间隙缓冲区还是没有命中，则通过 LRU 算法进行替换缓冲区
  struct buf* before_least = 0;   // 记录要替换的前一个块
  uint holding_bucket = -1;     // 记录当前持有哪个桶锁
  
  // 循环所有桶查找缓冲区进行替换
  for(int i = 0;i  < NBUFMAP_BUCKET; i++){
    acquire(&bcache.bufmap_locks[i]);   // 给当前桶上锁

    int newfound = 0;   // 是否在当前桶找到新的可以替换的缓冲区
    
    // 遍历当前桶查找可以替换的缓冲区
    for(b = &bcache.bufmap[i]; b->next ; b = b->next){
      if(b->next->refcnt == 0 && (!before_least || b->next->lastuse < before_least->next->lastuse)){
        before_least = b;
        newfound = 1;
      }
    }

    // 当前桶没找到
    if(!newfound)
      release(&bcache.bufmap_locks[i]);
    else{
      if(holding_bucket != -1)   // 如果不为 -1 ,说明之前持有桶锁,需要释放
        release(&bcache.bufmap_locks[holding_bucket]);
      holding_bucket = i;
    }
  }

  // 没找到可以替换的缓冲区
  if(!before_least)
    panic("bget: no buffers");

  b = before_least->next;

  // 如果要替换的桶不在 key 桶中，就要把那块缓冲区从其所在桶分离出来
  if(holding_bucket != key){
    before_least->next = b->next;
    release(&bcache.bufmap_locks[holding_bucket]);    // 要分离的缓冲区所在桶解锁
    
    // 要加入的桶上锁
    acquire(&bcache.bufmap_locks[key]);
    b->next = bcache.bufmap[key].next;
    bcache.bufmap[key].next = b;
  }

  // 设置新 buf 的字段
  b->dev = dev;
  b->blockno = blockno;
  b->refcnt = 1;
  b->valid = 0;

  // 释放相关锁
  release(&bcache.bufmap_locks[key]);
  release(&bcache.eviction_lock);
  acquiresleep(&b->lock);

  return b;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // 如果缓冲区的引用计数降为 0，则更新缓冲区的最后使用时间 lastuse 为当前的系统时间 ticks
    b->lastuse = ticks;
  }
  
  release(&bcache.bufmap_locks[key]);
}

void
bpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt++;
  release(&bcache.bufmap_locks[key]);
}

void
bunpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  release(&bcache.bufmap_locks[key]);
}


