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
void* initrefs();

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

#define GETFRAME(n) \
  (PGROUNDDOWN((uint64)n) - (uint64)end) / PGSIZE

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  uint32* refs;
} kmem;

void
kinit()
{
  void* start = initrefs();
  initlock(&kmem.lock, "kmem");
  freerange(start, (void*)PHYSTOP);
}

void*
initrefs()
{
  // aligns start and end and finds page number
  uint64 max = (PGROUNDDOWN(PHYSTOP) - PGROUNDUP((uint64)end)) / PGSIZE;
  uint64 p = PGROUNDUP((uint64)end);
  kmem.refs = 0;

  for (uint64 sz = 0; sz < sizeof(uint32)*max; sz += PGSIZE) {

    // panic if not enough memory
    if (p + PGSIZE >= PHYSTOP)
      panic("initrefs");

    // array pointer 
    if (!kmem.refs)
      kmem.refs = (uint32*) p;
    
    p += PGSIZE;
  }

  for (uint64 i = 0; i < max; i++) {
    kmem.refs[i] = 0;
  }

  return (void*) p;
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
  acquire(&kmem.lock);
  uint32 left = --kmem.refs[GETFRAME(pa)];
  release(&kmem.lock);

  if (!left)
    kfree((void*) pa);
}

void
increase_reference(uint64 pa) {
  acquire(&kmem.lock);
  kmem.refs[GETFRAME(pa)]++;
  release(&kmem.lock);
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
  uint64 frame = GETFRAME(pa);



  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  
  acquire(&kmem.lock);
  if ((left = kmem.refs[frame]))
    left = --kmem.refs[frame];
  release(&kmem.lock);
  
  if (left)
    return;

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
  if(r) {
    kmem.freelist = r->next;
    kmem.refs[GETFRAME(r)] = 1;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
