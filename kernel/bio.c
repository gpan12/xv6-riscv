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
#include "limits.h"

#define NBUCKETS 13

struct hb {
  struct spinlock lock;
  struct buf head;
};
struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  struct hb ht[NBUCKETS];
} bcache;

void
binit(void)
{
  initlock(&bcache.lock, "bcache");
  for (int i = 0; i < NBUCKETS; i++){
    initlock(&bcache.ht[i].lock, "bcache");
  }

  if (NBUF < NBUCKETS) {
    panic("nbuf less than nbuckets");
  }

  struct buf *b;

  for (int i = 0; i < NBUCKETS; i++) {
    bcache.ht[i].head.prev = &bcache.ht[i].head;
    bcache.ht[i].head.next = &bcache.ht[i].head;
  }

  int j = 0;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    int i_bucket = j % NBUCKETS;
    b->blockno = i_bucket;
    b->next = bcache.ht[i_bucket].head.next;
    b->prev = &bcache.ht[i_bucket].head;
    initsleeplock(&b->lock, "buffer");
    bcache.ht[i_bucket].head.next->prev = b;
    bcache.ht[i_bucket].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{

  // printf("Calling bget\n");

  int nbucket = blockno % NBUCKETS;  
  
  acquire(&bcache.ht[nbucket].lock);
  // printf("Acquired %d lock\n", nbucket);

  struct buf *b;

  for(b = bcache.ht[nbucket].head.next; b != &bcache.ht[nbucket].head; b = b->next){
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      // printf("Releasing %d lock\n", nbucket);
      release(&bcache.ht[nbucket].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // printf("Releasing %d lock\n", nbucket);
  release(&bcache.ht[nbucket].lock);
  acquire(&bcache.lock);
  // printf("Acquired bcache lock\n", nbucket);
  acquire(&bcache.ht[nbucket].lock);
  // printf("Acquired %d lock\n", nbucket);

  for(b = bcache.ht[nbucket].head.next; b != &bcache.ht[nbucket].head; b = b->next){
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      // printf("Releasing %d lock\n", nbucket);
      release(&bcache.ht[nbucket].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  uint timestamp = UINT_MAX;
  struct buf *buf_to_evict = 0;
  int evict_nbucket = -1;
  int candidate_evict_nbucket;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    candidate_evict_nbucket = b->blockno % NBUCKETS;
    if (candidate_evict_nbucket != nbucket &&
        candidate_evict_nbucket != evict_nbucket) {
      acquire(&bcache.ht[candidate_evict_nbucket].lock);
      // printf("Acquired candidate %d lock\n", candidate_evict_nbucket);
    }
    
    if (b->refcnt == 0 && b->timestamp < timestamp) {
      buf_to_evict = b;
      if (evict_nbucket != nbucket &&
          evict_nbucket != candidate_evict_nbucket &&
          evict_nbucket != -1){
        // printf("Releasing %d lock\n", evict_nbucket);
        release(&bcache.ht[evict_nbucket].lock);
      }
      evict_nbucket = buf_to_evict->blockno % NBUCKETS;
      timestamp = b->timestamp;
    } else {
        if (candidate_evict_nbucket != nbucket &&
          candidate_evict_nbucket != evict_nbucket) {
        // printf("Releasing candidate %d lock\n", candidate_evict_nbucket);
        release(&bcache.ht[candidate_evict_nbucket].lock);
      }
    }
  }

  if (buf_to_evict == 0) {
    panic("no buffers");
  }

  if (buf_to_evict != 0) {
    // already holding two bucket locks

    if (evict_nbucket != (buf_to_evict->blockno % NBUCKETS)) {
      panic("wrong evict nbucket");
    }

    if (!holding(&bcache.ht[evict_nbucket].lock)) {
      panic("not holding evict nbucket");
    }

    if (!holding(&bcache.ht[nbucket].lock)) {
      panic("not holding nbucket");
    }

    b = buf_to_evict;

    if (evict_nbucket != nbucket) {
      // printf("next: %p, previous: %p\n", b->next, b->prev);
      b->next->prev = b->prev;
      b->prev->next = b->next;
      b->next = bcache.ht[nbucket].head.next;
      b->prev = &bcache.ht[nbucket].head;
      bcache.ht[nbucket].head.next->prev = b;
      bcache.ht[nbucket].head.next = b;
    }

    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    // printf("Releasing %d lock\n", nbucket);
    release(&bcache.ht[nbucket].lock);
    if (evict_nbucket != nbucket) {
      // printf("Releasing evict %d lock\n", evict_nbucket);
      release(&bcache.ht[evict_nbucket].lock);
    }
    // printf("Releasing bcache lock\n");
    release(&bcache.lock);
    acquiresleep(&b->lock);
    return b;
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
  // printf("calling brelease\n");
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int nbucket = b->blockno % NBUCKETS;
  
  acquire(&bcache.ht[nbucket].lock);
  // printf("Acquired %d lock\n", nbucket);

  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    acquire(&tickslock);
    b->timestamp = ticks;
    release(&tickslock);
  }

  // printf("Releasing %d lock\n", nbucket);
  release(&bcache.ht[nbucket].lock);
}

void
bpin(struct buf *b) {
  int nbucket = b->blockno % NBUCKETS;
  acquire(&bcache.ht[nbucket].lock);
  // printf("Bpin acquired %d lock\n", nbucket);
  b->refcnt++;
  // printf("Bpin releasing %d lock\n", nbucket);
  release(&bcache.ht[nbucket].lock);
}

void
bunpin(struct buf *b) {
  int nbucket = b->blockno % NBUCKETS;
  acquire(&bcache.ht[nbucket].lock);
  // printf("Bunpin acquiring %d lock\n", nbucket);
  b->refcnt--;
  // printf("Bunpin releasing %d lock\n", nbucket);
  release(&bcache.ht[nbucket].lock);
}


