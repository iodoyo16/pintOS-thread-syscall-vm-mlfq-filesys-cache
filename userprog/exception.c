#include <inttypes.h>
#include <stdio.h>

#include "userprog/exception.h"
#include "userprog/gdt.h"
#include "userprog/syscall.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
/* 8MB */
#define MAX_STACK_SIZE 0x800000
/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);
void
exception_init (void) 
{
 
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

 
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

 
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}


void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

static void
kill (struct intr_frame *f) 
{
  switch (f->cs)
    {
    case SEL_UCSEG:
      printf ("%s: dying due to interrupt %#04x (%s).\n",
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
      thread_exit (); 

    case SEL_KCSEG:
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 

    default:
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}


static void
page_fault (struct intr_frame *f) 
{ 
  bool not_present;  /* not-present page / writing r/o page. */
  bool write;  /*  write access /  read access */
  bool user;   /* user  access/ kernel access. */      

  void *fault_addr;  
  asm ("movl %%cr2, %0" : "=r" (fault_addr));


  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

  #ifdef VM

  struct thread *curr = thread_current(); 
  void* fault_page = (void*) pg_round_down(fault_addr);
  if (!not_present) {// read only 페이지
    goto PAGE_FAULT_VIOLATED_ACCESS;
  }
  else{
    void* esp = user ? f->esp : curr->current_esp;

    // growin stack
    bool is_user_stack, is_push_avail;
    is_user_stack = (PHYS_BASE - MAX_STACK_SIZE <= fault_addr && fault_addr < PHYS_BASE);
    is_push_avail = (esp <= fault_addr || fault_addr == f->esp - 4 || fault_addr == f->esp - 32);
    if(vm_pt_look_up(curr->supt,fault_page)){
      handle_mm_fault(curr->supt, curr->pagedir, fault_page);
    }
    else if (is_user_stack && is_push_avail) {
      expand_stack(curr->supt, fault_page);
      handle_mm_fault(curr->supt, curr->pagedir, fault_page);
    }
    else{
      sys_exit(-1);
    }
    // success
    return;
  }
PAGE_FAULT_VIOLATED_ACCESS:
#endif
  if(!user) { // kernel mode
    f->eip = (void *) f->eax;
    f->eax = 0xffffffff;
    return;
  }

  printf ("Page fault at %p: %s error %s page in %s context.\n",
          fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading",
          user ? "user" : "kernel");
  sys_exit(-1);
}