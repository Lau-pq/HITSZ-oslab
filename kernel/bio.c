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
  //struct buf head;
  struct buf hashbucket[NBUCKETS]; //每个哈希队列一个linked list及一个lock
} bcache;

char bcache_lock_name[NBUCKETS][8];

int hash(int num) {
  return num % NBUCKETS;
}

void push(struct buf* head, struct buf* node) {
  node->prev = head;
  node->next = head->next;
  head->next->prev = node;
  head->next = node;
}

void pop(struct buf* node) {
  node->prev->next = node->next;
  node->next->prev = node->prev;
}

struct buf* find_lru_block(struct buf* head) {
  struct buf *b;
  for(b = head->prev; b != head; b = b->prev){
    if(b->refcnt == 0) {
      return b;
    }
  }
  return 0;
}

void init_blocks(struct buf* b, uint dev, uint blockno) {
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
}

void
binit(void)
{
  struct buf *b;

  for(int i = 0; i < NBUCKETS; i++) {
    snprintf(bcache_lock_name[i], 8, "bcache%d", i);
    initlock(&bcache.lock[i], bcache_lock_name[i]);
  }

  for (int i = 0; i < NBUCKETS; i++) {
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
  }
  // Create linked list of buffers

  for (int i = 0; i < NBUF; i++) {
    b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    push(&bcache.hashbucket[hash(i)], b);
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bucket_id = hash(blockno);
  acquire(&bcache.lock[bucket_id]);

  // Is the block already cached?
  for(b = bcache.hashbucket[bucket_id].next; b != &bcache.hashbucket[bucket_id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[bucket_id]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  b = find_lru_block(&bcache.hashbucket[bucket_id]);
  if(b) {
    init_blocks(b, dev, blockno);
    release(&bcache.lock[bucket_id]);
    acquiresleep(&b->lock);
    return b;
  }
  release(&bcache.lock[bucket_id]);
  
  for (int i = 0; i < NBUCKETS; i++) {
    if (i == bucket_id) continue;
    acquire(&bcache.lock[i]);
    b = find_lru_block(&bcache.hashbucket[i]);
    if (b) {
      pop(b);
      release(&bcache.lock[i]);
      acquire(&bcache.lock[bucket_id]);
      push(&bcache.hashbucket[bucket_id], b);
      init_blocks(b, dev, blockno);
      release(&bcache.lock[bucket_id]);
      acquiresleep(&b->lock);
      return b;
    }
    release(&bcache.lock[i]);
  }

  panic("bget: no buffers");

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

  int bucket_id = hash(b->blockno);
  acquire(&bcache.lock[bucket_id]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    pop(b);
    push(&bcache.hashbucket[bucket_id], b);
  }
  
  release(&bcache.lock[bucket_id]);
}

void
bpin(struct buf *b) {
  int bucket_id = hash(b->blockno);
  acquire(&bcache.lock[bucket_id]);
  b->refcnt++;
  release(&bcache.lock[bucket_id]);
}

void
bunpin(struct buf *b) {
  int bucket_id = hash(b->blockno);
  acquire(&bcache.lock[bucket_id]);
  b->refcnt--;
  release(&bcache.lock[bucket_id]);
}


