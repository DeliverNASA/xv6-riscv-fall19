// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// kalloc 最多可以分配的页面数目(从end到PHYSTOP)
#define NPAGE 32723
// #define NPAGE (PHYSTOP - (uint64)end) >> 12 + 1

char reference[NPAGE];
// char *reference;
// struct Record
// {
//   // struct spinlock lock;
//   uint8 counter[(PHYSTOP - KERNBASE) / PGSIZE]; //这里分配的计数器实际上会有点多，因为从BASE开始计数，但由于使用常数比较方便
// } record;


extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;


void freerange(void *pa_start, void *pa_end);

// 标记数组的操作函数
// 返回物理页的序号
int
getRefIndex(void *pa) {
  int index = ((char*)pa - (char*)PGROUNDUP((uint64)KERNBASE)) / PGSIZE;
  return index;
}
// 获取物理页的引用数
int getRef(void *pa)
{
  return reference[getRefIndex(pa)];
  // acquire(&record.lock);
  // return record.counter[getRefIndex(pa)];
  // release(&record.lock);
}
// 添加引用，用于fork时增加
void addRef(void *pa){
  reference[getRefIndex(pa)]++;
  // acquire(&record.lock);
  // record.counter[getRefIndex(pa)]++;
  // release(&record.lock);
}
// 减少引用，用于结束进程时
void subRef(void *pa) {
  int index = getRefIndex(pa);
  // acquire(&record.lock);
  // if(record.counter[index] != 0)
  //   record.counter[index]--;
  // release(&record.lock);
  if(reference[index] > 0)
    reference[index]--;
}



void
kinit()
{
  // initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    // 初始化ref_count
    // record.counter[getRefIndex(p)] = 0;
    reference[getRefIndex(p)] = 0;
    kfree(p);
  }
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

  // 减少当前物理页的引用数
  subRef(pa);
  int ref_count = getRef(pa);
  // 只有当没有引用之后，才能释放当前空间
  if(ref_count == 0) {
  // Fill with junk to catch dangling refs.
    memset(pa, 5, PGSIZE);
    r = (struct run*)pa;
    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
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

  // 修改的地方：申请物理页的时候添加引用
  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    int index = getRefIndex((void*)r);
    // acquire(&record.lock);
    // record.counter[index] = 1;
    reference[index] = 1;
    // release(&record.lock);
  }

  return (void*)r;
}