#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "userprog/process.h"

void syscall_init (void);

void sys_halt(void);
void sys_exit(int status);
pid_t sys_exec(const char *cmd_line);
int sys_wait(pid_t pid);

bool sys_create(const char *file_name, unsigned initial_size);
bool sys_remove(const char *file_name);

int sys_open(const char *file);
int sys_filesize(int fd);
int sys_read(int fd,void *buffer, unsigned size);
int sys_write(int fd, const void *buffer,unsigned size);
void sys_seek(int fd, unsigned position);
unsigned sys_tell(int fd);
void sys_close(int fd);

int fibonacci(int n);
int max_of_four_int(int a,int b, int c, int d);


#ifdef VM
void sys_munmap(mmapid_t);
mmapid_t sys_mmap(int fd, void *);
#endif


#endif /* userprog/syscall.h */