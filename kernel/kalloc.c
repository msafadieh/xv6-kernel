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
void initrefs(uint64 pa_start, uint64 pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int* list;
  uint64 offset;
} refs;

int refs_initialized = 0;

#define GETFRAME(n) \
  (PGROUNDDOWN((uint64)n) - refs.offset) / PGSIZE

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&refs.lock, "refs");
  initrefs((uint64) end, (uint64)PHYSTOP);
  freerange((void*)refs.offset, (void*)PHYSTOP);
}

void
initrefs(uint64 pa_start, uint64 pa_end)
{
  acquire(&refs.lock);
  int maxframe = 0;
  refs.list = (int*)PGROUNDUP(pa_start);

  for(uint64 p = (uint64) refs.list; p + PGSIZE <= pa_end; p += PGSIZE) {
    maxframe++;
  }

  refs.offset = (uint64) refs.list;
  for (uint64 sz = 0; sz < sizeof(int)*maxframe; sz += PGSIZE) {
    refs.offset += PGSIZE;
  }

  for (int i = 0; i < maxframe; i++) {
    refs.list[i] = 0;
  }

  refs_initialized = 1;
  release(&refs.lock);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

void
decrease_reference(uint64 pa) {
  int busy;

  acquire(&refs.lock);
  busy = --refs.list[GETFRAME(pa)];
  release(&refs.lock);

  if (!busy) {
    kfree((void*) pa);
  }

}

void
increase_reference(uint64 pa) {
  acquire(&refs.lock);
  refs.list[GETFRAME(pa)]++;
  release(&refs.lock);

}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int left;

  acquire(&refs.lock);
  if (refs_initialized) {
    left = --refs.list[GETFRAME(pa)];

    if (left < 0)
      refs.list[GETFRAME(pa)] = 0;
  } else {
    left = 0;
  }
  release(&refs.lock);

  if (left > 0) return;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk

      acquire(&refs.lock);
      if (refs_initialized)
        refs.list[GETFRAME(r)] = 1;
      release(&refs.lock);
  }
  return (void*)r;
}
