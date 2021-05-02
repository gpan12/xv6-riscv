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

struct memlist {
  struct spinlock lock;
  struct run *freelist;
};

struct memlist kmem[NCPU];

int
pa2memidx(void *pa) {
  int listSize = (int)(((uint64)end - PHYSTOP) / NCPU);
  int idx = (int)(((uint64)pa - PHYSTOP)) / listSize;
  return idx;
}

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    initlock(&kmem[i].lock, "kmem");
  }
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

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  int memidx = pa2memidx(pa);

  acquire(&(kmem[memidx].lock));
  r->next = kmem[memidx].freelist;
  kmem[memidx].freelist = r;
  release(&(kmem[memidx].lock));
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int id = cpuid();
  pop_off();

  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if (r) {
    kmem[id].freelist = r->next;
  }
  release(&kmem[id].lock);
  if (r) {
    memset((char*)r, 5, PGSIZE);
    return (void *)r;
  }

  // id CPU does not have a free page
  int i;
  for (i = 0; i < NCPU; i++) {
    acquire(&kmem[i].lock);
    r = kmem[i].freelist;
    if (r) {
      kmem[i].freelist = r -> next;
    }
    release(&kmem[i].lock);
    if (r) {
      memset((char*)r, 5, PGSIZE);
      return (void *)r;
    }
  }
  return (void*)r;
}
