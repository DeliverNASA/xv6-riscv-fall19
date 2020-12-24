#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;
  int newaddr;

  if(argint(0, &n) < 0)
    return -1;

  // 新页面的起始地址，恰好等于old sz
  addr = myproc()->sz;
  // if(growproc(n) < 0)
  //   return -1;

  // 下一个页的地址
  newaddr = PGROUNDUP(addr + n);
  // 超出地址空间，无法分配
  // 第一种情况：小于最低地址
  // 第二种情况：大于最高地址
  if(newaddr < PGSIZE || newaddr >= MAXVA) {
    exit(-1);
  }
  // n为负数的情况，看看是否能释放空间
  if(newaddr < addr) {
    uvmdealloc(myproc()->pagetable, addr, newaddr);
  }
  myproc()->sz = newaddr;

  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
