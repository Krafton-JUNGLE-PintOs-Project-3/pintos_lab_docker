#include "userprog/syscall.h"
#include <syscall-nr.h>
#include "threads/vaddr.h"
#include "lib/kernel/console.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/interrupt.h"
#include "../include/lib/string.h"
#include "threads/mmu.h"
#include "threads/palloc.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include <stdio.h>
#include "threads/thread.h"
#include "threads/synch.h"
#include "include/vm/vm.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);

int sys_read(int fd, void *buffer, size_t size);
int sys_write(int fd, void *buffer, size_t size);
void sys_close(int fd);
tid_t sys_fork(char* filename, struct intr_frame * if_);
int sys_wait(tid_t tid);
int sys_exec(const char *file);
unsigned sys_tell(int fd);
void sys_seek(int fd, unsigned position);
void sys_halt(void);
bool sys_create(char*filename, unsigned size);
int sys_open(char *filename);
bool sys_remove(char *filename);
int sys_filesize(int fd);
void check_valid_range(void *addr, size_t size);
static void check_writable_range(void *addr, size_t size);
void *sys_mmap (void *addr, size_t length, int writable, int fd, off_t offset);
void sys_munmap(void *addr);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.

	uint64_t syscall_type = f->R.rax;

	switch(syscall_type){
		case SYS_HALT:{
			sys_halt();
			break;
		}
		case SYS_EXIT:
			sys_exit(f->R.rdi);
			break;
		case SYS_EXEC:{
			int result = sys_exec(f->R.rdi);
			if(result == -1)
				sys_exit(-1);
			f->R.rax = result;	
			break;
		}
		case SYS_WAIT:
			f->R.rax = sys_wait(f->R.rdi);
			break;
		case SYS_OPEN:{
			char * filename = (char*)f->R.rdi;
			f->R.rax = sys_open(filename);
			break;
		}
		case SYS_REMOVE:{
			char* filename = (char*)f->R.rdi;
			f->R.rax=sys_remove(filename);
			break;
		}
		case SYS_WRITE:{
			int fd = (int)f->R.rdi;
			void *buf = (void*)f->R.rsi;
			size_t size = (size_t)f->R.rdx;

			f->R.rax = sys_write(fd, buf, size);
			break;
		}
		case SYS_READ:{
			int fd = (int) f->R.rdi;
			void *buf = (void*)f->R.rsi;
			size_t size = (size_t)f->R.rdx;
			f->R.rax= sys_read(fd,buf,size);
			break;
		}
		case SYS_FILESIZE:{
			f->R.rax = sys_filesize((int)f->R.rdi);
			break;
		}
		case SYS_CREATE:{
			char *filename = (char *)f->R.rdi;
			unsigned size = (unsigned)f->R.rsi;
			f->R.rax = sys_create(filename, size);
			break;
		}
		case SYS_FORK:{
			f->R.rax = sys_fork((char *)f->R.rdi, f);
			break;
		}
		case SYS_SEEK:{
			sys_seek((int)f->R.rdi,(unsigned)f->R.rsi);
			break;
		}
		case SYS_TELL:{
			f->R.rax = sys_tell((int)f->R.rdi);
			break;
		}
		case SYS_CLOSE: {
			int fd = (int) f->R.rdi;
			sys_close(fd);
			break;
		}
		case SYS_MMAP:{
			f->R.rax = sys_mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
            break;
		}
		case SYS_MUNMAP:{
			sys_munmap(f->R.rdi);
			break;
		}
		// default:
        //     sys_exit(-1);
	}
	// thread_exit ();

}

void
sys_halt(void){
	power_off();
}

int 
sys_wait(tid_t pid){
	return process_wait(pid);
}

int
sys_exec(const char *file){

	check_address(file);

	char *file_name = palloc_get_page(PAL_ZERO);
	if (file_name == NULL){
		palloc_free_page(file_name);
		file_name = NULL;
		sys_exit(-1);
	}


	strlcpy(file_name, file, PGSIZE); //copy file, user->kernal

	if (process_exec(file_name) == -1){
		sys_exit(-1);
	}
	NOT_REACHED();
	return -1;
}

tid_t
sys_fork(char *thread_name, struct intr_frame *if_){

	check_address(thread_name);

	tid_t child_tid = process_fork(thread_name, if_);
	
	if(child_tid < 0){
		return TID_ERROR;
	}

	return child_tid;
}

bool
sys_create(char* filename, unsigned size){

	check_address(filename);

	if(strlen(filename) > 14) return 0;

	size_t init_size = (size_t) size;
	lock_acquire(&file_lock);
	bool result = filesys_create(filename, init_size);
	lock_release(&file_lock);
	return result;
}

int
sys_open(char* filename){

	check_address(filename);

	struct thread *cur = thread_current();
	//file descriptor 할당
	int fd = find_descriptor(cur);
	if(fd == -1){
		return -1;
	}
    
	// enum intr_level old = intr_disable();
	lock_acquire(&file_lock);
	struct file* file = filesys_open(filename);
	lock_release(&file_lock);
	// intr_set_level(old);
	if(file == NULL){
		// sys_exit(-1);
		return -1;
	}
	
	cur->file_table[fd] = file;
	return fd;
}

int
sys_filesize(int fd){
	struct file *file_addr = is_open_file(thread_current(),fd);
	if(file_addr == NULL)
		return -1;

	lock_acquire(&file_lock);
	off_t size = file_length(file_addr);
	lock_release(&file_lock);
	return size;
}

void
sys_exit(int status){
	struct thread *cur = thread_current();
	struct child *c;
	c = cur->my_self;

	if ( c != NULL){ //exit status가 -1이 아니고 child가 존재할 때 
		c->is_exit = true;			//child 구조체 안에 값들 수정
		c->exit_status = status;
		printf("%s: exit(%d)\n",cur->name,status);//로그
		sema_up(&c->sema);
	}
	thread_exit();
}

int
sys_read(int fd, void *buffer, size_t size){
	struct thread* curr = thread_current();
	if(size == 0){
		return 0;
	}
	check_valid_range(buffer,size);
	check_writable_range(buffer,size);
	

	if((fd<0) || (fd>=127)){
		return -1;
	}

	if(fd == 0){
		char *buf = (char *) buffer;
		// lock_acquire(&file_lock);
		for(int i=0;i<size;i++){
			buf[i] = input_getc();
		}
		// lock_release(&file_lock);
		return size;
	}else{
		struct thread* cur = thread_current();
		struct file *file = is_open_file(cur,fd);

		if(file == NULL){
			return -1;
		}
		// check_writable_range(buffer,size);
		lock_acquire(&file_lock);
		off_t result = file_read(file, buffer, size);
		lock_release(&file_lock);
		return result;
	}
}

int
sys_write(int fd, void* buf, size_t size){
	check_valid_range(buf,size);
	if((fd<=0) || (fd>=127)){
		return -1;
	}

	if(fd == 1){
		// lock_acquire(&file_lock);
		putbuf((char *)buf, size);
		// lock_release(&file_lock);
		return size;
	}else if(fd >= 2){
		// file descriptor 
		struct thread* curr = thread_current();
		struct file* file_addr = is_open_file(curr, fd);
		
		if(file_addr == NULL){
			return -1;
		}

		lock_acquire(&file_lock);
		int32_t written = file_write(file_addr, buf, size);
		lock_release(&file_lock);
		if(written < 0) return -1;

		return written;
	}
	return -1;
}

void
sys_close(int fd){
	struct thread *cur = thread_current();
	struct file *file = is_open_file(cur, fd);

	if(file == NULL)
		return;

	lock_acquire(&file_lock);
	file_close(file);	
	lock_release(&file_lock);
	cur->file_table[fd] = NULL;
}

bool
sys_remove(char* filename){
	
	check_address(filename);

	lock_acquire(&file_lock);
	int result = filesys_remove(filename);
	lock_release(&file_lock);

	return result;
}

void
sys_seek(int fd, unsigned position){
	struct file *f = thread_current()->file_table[fd];
	if (f == NULL)
		return;
	file_seek(f, position);
}

unsigned
sys_tell(int fd){
	struct file *f = thread_current()->file_table[fd];
	if (f == NULL)
		return (unsigned)-1;
	file_tell(f);
}


#ifndef VM
void check_address(void *addr){
	struct thread *curr = thread_current();

	if(!is_user_vaddr(addr) || addr == NULL ||pml4_get_page(curr->pml4, addr) == NULL){
		sys_exit(-1);
	}
}

#else
void *sys_mmap (void *addr, size_t length, int writable, int fd, off_t offset){
	//유효성 검사를 다해야 할듯
	//fd로 열린 파일의 오프셋 바이트부터 length 바이트 만큼을 프로세스의 가상주소 공간의 주소 addr에 매핑한다.
	//전체 파일은 addr에서 시작하는 연속 가상 페이지에 매핑된다.
	if (!addr || !is_user_vaddr(addr) || !is_user_vaddr(addr + length-1))
        return NULL;

	if((uintptr_t)addr % 4096 != 0){
		return NULL;
		
	}
	if((uintptr_t)offset % 4096 != 0){
		return NULL;
		
	}

	if(fd == 0 || fd == 1){
		return NULL;
		
	}

	struct thread* curr = thread_current();
	struct file* open_file = is_open_file(curr, fd);
	if(open_file == NULL || length == 0){
		return NULL;

	}
	lock_acquire(&file_lock);
	struct file *reopen_file = file_reopen(open_file); //각 매핑에 대해 파일에 대한 별도의 독립적인 참조를 얻으려면 이 함수를 사용해야합니다.
	lock_release(&file_lock);
    if (reopen_file == NULL) {
        return NULL;
    }
	return do_mmap(addr, length, writable, reopen_file, offset);
}


void sys_munmap(void *addr){
	do_munmap(addr);
	return;
}




static void
check_writable_range(void *addr, size_t size) {
	uint8_t *ptr = addr;
	struct thread *curr = thread_current();
	struct supplemental_page_table *spt = &curr->spt;
	for (size_t i = 0; i < size; i++) {
		struct page *page = spt_find_page(spt, ptr + i);
		if (page == NULL || !page->page_writable) {
			sys_exit(-1);  // 보안 위반 시 즉시 종료
		}
	}
}

struct page *check_address(void *addr){
	// struct thread *curr = thread_current();

	// if(!is_user_vaddr(addr) || addr == NULL || !spt_find_page(&curr->spt, addr) || !is_user_vaddr(addr+PGSIZE-1)){
	// 	sys_exit(-1);
	// }

	// return spt_find_page(&curr->spt, addr);
	struct thread *curr = thread_current();
	if (addr == NULL || !is_user_vaddr(addr)){
		sys_exit(-1);
	}


	struct page *page = spt_find_page(&curr->spt, addr);
	if (page == NULL){
		sys_exit(-1);
	}
		
	if(pml4_get_page(curr->pml4, addr) == NULL){
			if(!vm_claim_page(addr)){
				sys_exit(-1);
			}
	}
	return page;
}


void check_valid_range(void *addr, size_t size) {
    uint8_t *start = (uint8_t *)pg_round_down(addr);
    uint8_t *end = (uint8_t *)pg_round_down(addr + size - 1);

    for (uint8_t *p = start; p <= end; p += PGSIZE) {
        if (!is_user_vaddr(p))
            sys_exit(-1);
        struct page *page = spt_find_page(&thread_current()->spt, p);
        	if (page == NULL)
		sys_exit(-1);
		if(pml4_get_page(thread_current()->pml4, p) == NULL){
			if(!vm_claim_page(p)){
				sys_exit(-1);
			}
		}
    }
}
#endif