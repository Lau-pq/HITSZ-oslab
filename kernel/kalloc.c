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

struct kmem{
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmems[NCPU];

char lock_name[NCPU][8];

void
kinit()
{
  for (int i = 0; i < NCPU; i++) {
    snprintf(lock_name[i], 8, "kmem%d", i);
    initlock(&kmems[i].lock, lock_name[i]);
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

void kfree_cpu(void *pa, int cpu_id) {
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmems[cpu_id].lock);
  r->next = kmems[cpu_id].freelist;
  kmems[cpu_id].freelist = r;
  release(&kmems[cpu_id].lock);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  push_off();
  int id = cpuid();
  pop_off();
  kfree_cpu(pa, id);
}


void* kalloc_cpu(int cpu_id) {
  struct run *r;

  acquire(&kmems[cpu_id].lock);
  r = kmems[cpu_id].freelist;
  if(r) {
    kmems[cpu_id].freelist = r->next;
  }
  release(&kmems[cpu_id].lock);

  if (!r) {
    for (int i = 0; i < NCPU; i++) {
      if (i == cpu_id) continue;
      acquire(&kmems[i].lock);
      r = kmems[i].freelist;
      if (r) {
        kmems[i].freelist = r->next;
        release(&kmems[i].lock);
        break;
      } else {
        release(&kmems[i].lock);
      }
    }
  }

  if (r) {
    memset((char*)r, 5, PGSIZE);
  }
  return (void*)r;
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
  r = kalloc_cpu(id);
  return (void*)r;
}