#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#ifndef VM
void check_address(void *addr);
#else
struct page *check_address(void *addr);
#endif

struct lock fd_lock;

void syscall_init (void);

void sys_exit(int status);

#endif /* userprog/syscall.h */
