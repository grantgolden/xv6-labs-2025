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
  int page_cnt;
};

//struct kallocator kmem;


struct kallocator kmems[NCPU];

static char *lk_name[] = {"kmem_c0", "kmem_c1", "kmem_c2", "kmem_c3", "kmem_c4", "kmem_c5", "kmem_c6", "kmem_c7"};

void
kinit()
{
  memset(kmems, 0, sizeof(kmems));

  for (int i = 0; i < sizeof(kmems)/sizeof(kmems[0]); i++)
    initlock(&kmems[i].lock, lk_name[i]);

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
  pop_off();

  acquire(&kmem->lock);
  r->next = kmem->freelist;
  kmem->freelist = r;
  kmem->page_cnt++;
  release(&kmem->lock);
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
  pop_off();

  acquire(&kmem->lock);
  r = kmem->freelist;
  if(r) {
    kmem->freelist = r->next;
    kmem->page_cnt--;
    release(&kmem->lock);
  } else { //current freelist is empty
    release(&kmem->lock);
    int free_cnt_max = -1;
    int steal_cpu = 0;
    for (int i = 0; i < sizeof(kmems)/sizeof(kmems[0]); i++) {
      if (i == id)
        continue;
      acquire(&kmems[i].lock);
      if (kmems[i].page_cnt > free_cnt_max) {
        free_cnt_max = kmems[i].page_cnt;
        steal_cpu = i;
      }
      release(&kmems[i].lock);
    }

    //steal pages from cpu with has most free pages
    struct kallocator *steal_kmem = &kmems[steal_cpu];
    acquire(&steal_kmem->lock);

    struct run *r_s = steal_kmem->freelist;
    struct run *r_temp = 0;

    for (int i = 0; i < free_cnt_max/2; i++) {
      r_temp = steal_kmem->freelist;
      if (r_temp) {
        steal_kmem->freelist = r_temp->next;
        steal_kmem->page_cnt--;
      } else
        break;
    }

    if (r_temp)
      r_temp->next = 0;

    release(&steal_kmem->lock);

    acquire(&kmem->lock);
    while (r_s) {
      r_temp = r_s->next;
      r_s->next = kmem->freelist;
      kmem->freelist = r_s;
      kmem->page_cnt++;
      r_s = r_temp;
    }

    r = kmem->freelist;

    if (r) {
      kmem->freelist = r->next;
      kmem->page_cnt--;
    }
    release(&kmem->lock);
    //  if (id == i)
    //    continue;
    //  //get the cpu which has most free pages
    //  kmem = &kmems[i];
    //  acquire(&kmem->lock);
    //  r = kmem->freelist;
    //  if (r) { //get free page
    //    kmem->freelist = r->next;
    //    release(&kmem->lock);
    //    break;
    //  }
    //  release(&kmem->lock);
    //}
  }



  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
