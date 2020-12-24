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

#define NBUCKETS 13

struct {
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf hashbucket[NBUCKETS];
} bcache;

void
binit(void)
{
  struct buf *b;
  // 初始化哈希
  for(int i = 0; i < NBUCKETS; i++) {
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
    initlock(&bcache.lock[i], "bcache.lock");
  }

  // Create linked list of buffers
  // 头插法创建链表
  for(b = bcache.buf; b < bcache.buf + NBUF; b++) {
    b->next = bcache.hashbucket[0].next;
    b->prev = &bcache.hashbucket[0];
    initsleeplock(&b->lock, "bcache.buffer.lock");
    bcache.hashbucket[0].next->prev = b;
    bcache.hashbucket[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint id = blockno % NBUCKETS;
  acquire(&bcache.lock[id]);

  // Is the block already cached?
  // 如果已经存在，添加引用
  for(b = bcache.hashbucket[id].next; b != &bcache.hashbucket[id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[id]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached; recycle an unused buffer.
  // 如果不存在，选择合适的buffer分配
  for(b = bcache.hashbucket[id].prev; b != &bcache.hashbucket[id]; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[id]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // while lacking of unused buffer, steal buffer from other hashbuckets 
  // 偷取其他的buffer
  for(int i = 0; i < NBUCKETS; i++) {
    if(i == id) continue;
    acquire(&bcache.lock[i]);
    for(b = bcache.hashbucket[i].prev; b != &bcache.hashbucket[i]; b = b->prev){
      if(b->refcnt == 0) {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;

        // change original list
        // 修改自身的buffer表
        b->next->prev = b->prev;
        b->prev->next = b->next;

        // change current list
        // 修改偷取对象的buffer表
        b->next = bcache.hashbucket[id].next;
        b->prev = &bcache.hashbucket[id];
        bcache.hashbucket[id].next->prev = b;
        bcache.hashbucket[id].next = b;

        release(&bcache.lock[id]);
        release(&bcache.lock[i]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    // if still lacks of unused buffer, we also need to release
    release(&bcache.lock[i]);
  }
  release(&bcache.lock[id]);
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b->dev, b, 0);
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
  virtio_disk_rw(b->dev, b, 1);
}

// Release a locked buffer.
// Move to the head of the MRU list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint id = (*b).blockno % NBUCKETS;
  acquire(&bcache.lock[id]);
  b->refcnt--;
  // 如果引用数为0，释放buffer，添加到头部后面
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.hashbucket[id].next;
    b->prev = &bcache.hashbucket[id];
    bcache.hashbucket[id].next->prev = b;
    bcache.hashbucket[id].next = b;
  }
  
  release(&bcache.lock[id]);
}

void
bpin(struct buf *b) {
  uint id = (*b).blockno % NBUCKETS;
  acquire(&bcache.lock[id]);
  b->refcnt++;
  release(&bcache.lock[id]);
}

void
bunpin(struct buf *b) {
  uint id = (*b).blockno % NBUCKETS;
  acquire(&bcache.lock[id]);
  b->refcnt--;
  release(&bcache.lock[id]);
}


