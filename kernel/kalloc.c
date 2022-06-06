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
extern uint64 cas(volatile void *address, int prev, int next);

struct run
{
  struct run *next;
};

struct
{
  struct spinlock lock;
  struct run *freelist;
  uint64 count_of_pages[((PHYSTOP - KERNBASE) / PGSIZE)];
} kmem;

void kinit()
{
  initlock(&kmem.lock, "kmem");
  memset(kmem.count_of_pages, 0, sizeof(kmem.count_of_pages));
  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
  char *p = (char *)PGROUNDUP((uint64)pa_start);
  uint64 index = (((uint64)p - KERNBASE) / PGSIZE);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
  {
    kmem.count_of_pages[index] = 1;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  if (dec_ref_count(pa) > 0)
    return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

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
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk

  if (r)
    inc_ref_count((uint64)r);

  return (void *)r;
}

int inc_ref_count(uint64 pa)
{
  uint64 place = pa - KERNBASE;
  place = place / PGSIZE;
  while (cas(kmem.count_of_pages + place, kmem.count_of_pages[place], kmem.count_of_pages[place] + 1))
    ;
  return get_ref_count(pa);
}

int dec_ref_count(uint64 pa)
{
  uint64 place = pa - KERNBASE;
  place = place / PGSIZE;
  while (cas(kmem.count_of_pages + place, kmem.count_of_pages[place], kmem.count_of_pages[place] - 1))
    ;
  return get_ref_count(pa);
}

int get_ref_count(uint64 pa)
{
  uint64 place = pa - KERNBASE;
  place = place / PGSIZE;
  return kmem.count_of_pages[place];
}
