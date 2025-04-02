// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;   // 空闲物理页链表
} kmem[NCPU];   // 为每个 CPU 分配独立的 freelist，并用独立的锁保护它

// 定义锁的名字数组
char *kmem_lock_names[] = {
  "kmem_cpu_0",
  "kmem_cpu_1",
  "kmem_cpu_2",
  "kmem_cpu_3",
  "kmem_cpu_4",
  "kmem_cpu_5",
  "kmem_cpu_6",
  "kmem_cpu_7",
};

void
kinit()
{
  for(int i = 0; i < NCPU; i++)
    initlock(&kmem[i].lock, kmem_lock_names[i]);    // 第一个参数是对应CPU的锁,第二个参数是锁的名字
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  // 内存页是否对齐  || 不低于内核结束地址 || 不超过物理内存上线
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  // 填充垃圾数据，帮助捕获悬挂引用（为了调试，防止程序访问已经释放的内存区域）
  // 悬挂引用：指程序中存在指针指向已经释放内存的地址
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();   // 关闭中断

  int cpu = cpuid();   // 获取 cpu 编号.中断关闭时再获取 cpuid 才是安全的,所以要先用 push_off 关闭中断

  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);

  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// 该函数没有参数,返回一个 void* 类型的指针，指向分配到的内存页
void *
kalloc(void)
{
  struct run *r;

  push_off();

  int cpu = cpuid();

  acquire(&kmem[cpu].lock);
  // 需要先判断当前 cpu 的 freelist是否有空闲物理页，没有的话需要去其他 cpu 那里偷页
  if(!kmem[cpu].freelist){
    int steal_left = 64;      // 设定要偷的内存页

    for(int i = 0; i < NCPU; i++){
      if(i == cpu)    // 跳过当前 cpu
        continue;

      acquire(&kmem[i].lock);     // 申请要 steal 的 cpu 的锁
      if(!kmem[i].freelist){      // 要偷的 cpu 也没有 freelist 了，就释放锁直接跳过
        release(&kmem[i].lock);
        continue;
      }   

      struct run* rr = kmem[i].freelist;
      while(rr && steal_left){
        kmem[i].freelist = rr->next;
        rr->next = kmem[cpu].freelist;
        kmem[cpu].freelist = rr;
        rr = kmem[i].freelist;
        steal_left--;
      }
      
      release(&kmem[i].lock);

      // 可能这个cpu未满足需要 steal 的内存页,所以要有这个判断条件
      if(steal_left == 0)
        break;
    }
  }

  // 分配物理页
  r = kmem[cpu].freelist;
  if(r)
    kmem[cpu].freelist = r->next;
  release(&kmem[cpu].lock);

  pop_off();    // 打开中断

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk,填充垃圾数据，帮助捕获悬挂引用
  return (void*)r;
}
