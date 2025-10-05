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

//reference count to physical pages
int page_refcnt[(PHYSTOP - KERNBASE)/PGSIZE + 1];

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int *refcnt;
} page_reference;

void
kinit()
{
  initlock(&page_reference.lock, "page_refcnt");
  page_reference.refcnt = page_refcnt;

  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  for (int i = 0; i < sizeof(page_refcnt)/sizeof(page_refcnt[0]); i++)
    page_refcnt[i] = 0;

  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  if (get_pg_refcnt(pa)) //phsical page still refered
      return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void incr_pg_refcnt(void *pa)
{
  acquire(&page_reference.lock);
  page_reference.refcnt[((uint64)pa - KERNBASE)/4096]++;
  release(&page_reference.lock);
}

void decr_pg_refcnt(void *pa)
{
  acquire(&page_reference.lock);
  page_reference.refcnt[((uint64)pa - KERNBASE)/4096]--;
  release(&page_reference.lock);
}

int get_pg_refcnt(void *pa)
{
  return page_reference.refcnt[((uint64)pa - KERNBASE)/4096];
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
    incr_pg_refcnt((void*)r);
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
