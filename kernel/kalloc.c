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


struct kallocator{
  struct spinlock lock;
  struct run *freelist;
};

//struct kallocator kmem;


struct kallocator kmems[NCPU];

static char *lk_name[] = {"kmem_c0", "kmem_c1", "kmem_c2", "kmem_c3", "kmem_c4", "kmem_c5", "kmem_c6", "kmem_c7"};

void
kinit()
{
  memset(kmems, 0, sizeof(kmems));
  freerange(end, (void*)PHYSTOP);

  for (int i = 0; i < sizeof(kmems)/sizeof(kmems[0]); i++) {
    initlock(&kmems[i].lock, lk_name[i]);
  }
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
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
  struct kallocator *kmem;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  kmem = &kmems[cpuid()];

  acquire(&kmem->lock);
  r->next = kmem->freelist;
  kmem->freelist = r;
  release(&kmem->lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  struct kallocator *kmem;
  int id;

  push_off();
  id = cpuid();
  kmem = &kmems[id];

  acquire(&kmem->lock);
  r = kmem->freelist;

  if (r) {
    kmem->freelist = r->next;
    release(&kmem->lock);
  } else { //current freelist is empty, stealing from other cpus
    release(&kmem->lock);

    int steal_left = 64;

    struct run *r_steal = 0;

    for (int i = 0; i < sizeof(kmems)/sizeof(kmems[0]); i++) {

      if (i == id)
        continue;

      struct kallocator *steal_kmem = &kmems[i];

      acquire(&steal_kmem->lock);

      struct run *rt = steal_kmem->freelist;
      while (rt && steal_left) {
        steal_kmem->freelist = rt->next;
        rt->next = r_steal;
        r_steal = rt;
        rt = steal_kmem->freelist;
        steal_left--;
      }
      release(&steal_kmem->lock);

      if (!steal_left)
        break;
    }

    acquire(&kmem->lock);
    kmem->freelist = r_steal;
    r = kmem->freelist;

    if (r) {
      kmem->freelist = r->next;
    }
    release(&kmem->lock);

  }

  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
