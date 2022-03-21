#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H
#define INPUT_ARG_MAX 128
#define WORD_SIZE 4
#include <stdio.h>
#include "threads/thread.h"
#include "threads/synch.h"


typedef int pid_t;
#define PID_ERROR ((pid_t) -1)        
#define PID_INIT ((pid_t) -2)

struct process_control_block {
    pid_t pid;

    const char *cmdline;

    struct list_elem elem;
    struct thread *parent_thread;

    bool waiting;   //waiting flag
    bool exited;    //exit flag
    bool orphan;    //orphan flag
    int32_t exitcode;
    struct semaphore sema_init; 
    struct semaphore sema_wait; 
};

struct fd_struct {
    int id;
    struct dir* dir;
    struct list_elem elem;
    struct file* file;
};

#ifdef VM
typedef int mmapid_t;

struct mmap_desc {
  mmapid_t id;
  struct list_elem elem;
  struct file* file;

  void *addr;   
  size_t size;  
};
#endif

struct process_control_block *find_child_process(pid_t pid);
void push_userstack(const char** parsed_filename_argv, int argc,void **esp);
int parse_file_name(char *input, const char **parsed_filename_argv);

pid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);


#endif /* userprog/process.h */
