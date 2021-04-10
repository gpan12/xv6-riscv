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
extern struct proc proc[NPROC];

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
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2) {
    struct proc *np;  
    for(np = proc; np < &proc[NPROC]; np++){
      // printf("%p has alarm %d\n", np, np->has_alarm);
      if (!(np->has_alarm))
        continue;
      if (np->alarm_executing)
        continue;
      if (np->ticks_left != 0) {
        np->ticks_left--;
        continue;
      }
      // printf("Modifying trapframe\n");
      // save current registers

      // printf("%p\n", np->trapframe->a0);
      // printf("%p\n", np->alarm_trapframe->a0);

      np->alarm_trapframe->a0 = np->trapframe->a0;
      np->alarm_trapframe->a1 = np->trapframe->a1;
      np->alarm_trapframe->a2 = np->trapframe->a2;
      np->alarm_trapframe->a3 = np->trapframe->a3;
      np->alarm_trapframe->a4 = np->trapframe->a4;
      np->alarm_trapframe->a5 = np->trapframe->a5;
      np->alarm_trapframe->a6 = np->trapframe->a6;
      np->alarm_trapframe->a7 = np->trapframe->a7;
      np->alarm_trapframe->t0 = np->trapframe->t0;
      np->alarm_trapframe->t1 = np->trapframe->t1;
      np->alarm_trapframe->t2 = np->trapframe->t2;
      np->alarm_trapframe->t3 = np->trapframe->t3;
      np->alarm_trapframe->t4 = np->trapframe->t4;
      np->alarm_trapframe->t5 = np->trapframe->t5;
      np->alarm_trapframe->t6 = np->trapframe->t6;
      np->alarm_trapframe->s0 = np->trapframe->s0;
      np->alarm_trapframe->s1 = np->trapframe->s1;
      np->alarm_trapframe->s2 = np->trapframe->s2;
      np->alarm_trapframe->s3 = np->trapframe->s3;
      np->alarm_trapframe->s4 = np->trapframe->s4;
      np->alarm_trapframe->s5 = np->trapframe->s5;
      np->alarm_trapframe->s6 = np->trapframe->s6;
      np->alarm_trapframe->s7 = np->trapframe->s7;
      np->alarm_trapframe->s8 = np->trapframe->s8;
      np->alarm_trapframe->s9 = np->trapframe->s9;
      np->alarm_trapframe->s10 = np->trapframe->s10;
      np->alarm_trapframe->s11 = np->trapframe->s11;
      np->alarm_trapframe->epc = np->trapframe->epc;
      np->alarm_trapframe->ra = np->trapframe->ra;
      np->alarm_trapframe->sp = np->trapframe->sp;
      np->alarm_trapframe->gp = np->trapframe->gp;
      np->alarm_trapframe->tp = np->trapframe->tp;

      np->alarm_executing = 1;

      np->trapframe->epc = (uint64) np->alarm_handler;
    }
    yield();
  }

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

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
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
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

