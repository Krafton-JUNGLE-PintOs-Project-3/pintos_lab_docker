#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H


#endif /* userprog/syscall.h */

#ifndef VM
void check_address(void *addr);
#else
void check_address(void *addr);
#endif


void syscall_init (void);

void sys_exit(int status);
