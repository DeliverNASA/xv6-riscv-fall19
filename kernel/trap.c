#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->tf->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->tf->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } 
  // 修改部分：判别页分配错误
  // 15是缺页中断的编号
  else if(r_scause() == 15){
    pte_t* pte;
    uint64 va = PGROUNDDOWN(r_stval());

    if(va >= MAXVA){
      printf("usertrap: va is larger than MAXVA!\n");
      p->killed = 1;
      goto end;
    }

    if(va > p->sz){
      printf("usertrap: va is larger than sz!\n");
      p->killed = 1;
      goto end;
    }

    pte = walk(p->pagetable, va, 0);

    // 当前页表是空的或者不是COW类型，都不能继续执行
    // 默认只有为child分配内存的时候才会陷入该中断
    // 因此要求指针存在，但是标志位PTE_COW有效，PTE_W无效
    if(pte == 0 || ((*pte) & PTE_COW) == 0 || ((*pte) & PTE_V) == 0 || ((*pte) & PTE_U) == 0) {
      printf("usertrap: pte not exist or it's not cow page\n");
      p->killed = 1;
      goto end;
    }

    // // 处理COW导致的页中断
    // if(*pte & PTE_COW){
    //   char *mem;
    //   // 申请新的物理内存
    //   if((mem = kalloc()) == 0) {
    //     printf("usertrap(): memery alloc fault\n");
    //     p->killed = 1;
    //     goto end;
    //   }
    //   memset(mem, 0, PGSIZE);
    //   uint64 pa = walkaddr(p->pagetable, va);

    //   // 判别物理地址是否存在
    //   if(pa) {
    //     memmove(mem, (char*)pa, PGSIZE);
    //     int perm = PTE_FLAGS(*pte);
    //     perm |= PTE_W;
    //     perm &= ~PTE_COW;
    //     // 判断印射是否成功
    //     if(mappages(p->pagetable, va, PGSIZE, (uint64)mem, perm) != 0) {
    //       printf("usertrap: can not map page\n");
    //       kfree(mem);
    //       p->killed = 1;
    //       goto end;
    //     }
    //     kfree((void*) pa);
    //   }
    // }
    if(*pte & PTE_COW) {
      // 一下部分代码来自原来vm中的uvmcopy
      char *mem;
      if((mem = kalloc()) == 0) {
        printf("usertrap: allocate memory failed.\n");
        p->killed = 1;
        goto end;
      }
      // 找到当前物理地址，然后复制过来
      uint64 pa = walkaddr(p->pagetable, va);
      if(pa) {
        memmove(mem, (char*)pa, PGSIZE);
        // 然后修改标志位
        int flags = PTE_FLAGS(*pte);
        flags &= ~PTE_COW;
        flags |= PTE_W;
        // 将当前进程的页面和物理空间进行映射
        if(mappages(p->pagetable, va, PGSIZE, (uint64)mem, flags) != 0){
          printf("usertrap: map new address failed.\n");
          kfree(mem);
          p->killed = 1;
          goto end;
        }
        kfree((void*) pa);
      }
    }
    else {
      printf("usertrap: not cause by cow.\n");
      p->killed = 1;
      goto end;
    }
  }
  else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }
end:
  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // turn off interrupts, since we're switching
  // now from kerneltrap() to usertrap().
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->tf->kernel_satp = r_satp();         // kernel page table
  p->tf->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->tf->kernel_trap = (uint64)usertrap;
  p->tf->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->tf->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
// must be 4-byte aligned to fit in stvec.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ || irq == VIRTIO1_IRQ ){
      virtio_disk_intr(irq - VIRTIO0_IRQ);
    }

    plic_complete(irq);
    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

