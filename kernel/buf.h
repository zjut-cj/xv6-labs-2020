struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;     // 设备编号
  uint blockno;   // 磁盘块号
  struct sleeplock lock;
  uint refcnt;    // 引用计数
  // struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];

  uint lastuse;   // 该缓冲区的最后使用时间（通常用于实现 LRU 策略）
};

