#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

static struct fork_aux{
	struct thread* parent;
	struct child* child_info;
	struct intr_frame *frame;
};

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* FILE_NAME에서 로드된 "initd"라는 첫 번째 사용자 영역 프로그램을 시작합니다.  
   새로 생성된 스레드는 process_create_initd()가 반환되기 전에  
   스케줄링되거나 심지어 종료될 수도 있습니다.  
   initd의 스레드 ID를 반환하며, 스레드를 생성할 수 없을 경우 TID_ERROR를 반환합니다.  
   ※ 이 함수는 **한 번만 호출되어야 합니다. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * 그렇지 않으면 호출자와 load() 함수 사이에  
       레이스 조건(race condition)이 발생할 수 있습니다.*/
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	char *save_ptr;
	char * name = strtok_r(file_name, " ", &save_ptr);

	/* FILE_NAME을 실행할 새로운 스레드를 생성합니다. */
	tid = thread_create (name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR){
		palloc_free_page (fn_copy);
		fn_copy = NULL;
	}
	return tid;
}

/* 첫 번째 사용자 프로세스를 실행하는 스레드 함수입니다. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* 현재 프로세스를 `name`이라는 이름으로 복제(clone)합니다.  
   새로운 프로세스의 스레드 ID를 반환하며,  
   스레드를 생성할 수 없는 경우에는 TID_ERROR를 반환합니다. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* 현재 스레드를 새로운 스레드로 복제합니다. */ 
	struct thread *cur = thread_current();

	struct fork_aux* aux = malloc(sizeof(struct fork_aux));
	aux->parent= cur;
	aux->frame = if_;

	// 첫 번째 인자로 설정된 __do_fork 실행하고 현재 스레드 즉 부모 스레드를 두 번째 인자로 가진다.
	tid_t child_tid = thread_create (name, PRI_DEFAULT, __do_fork, aux);

	if(child_tid < 0){
		free(aux);
		aux = NULL;
		return TID_ERROR;
	}

	struct list_elem *e;

	for(e=list_begin(&cur->child_list);e!=list_end(&cur->child_list);e=list_next(e)){
		struct child* ch = list_entry(e, struct child, elem);
		if(ch->child_tid == child_tid){
			sema_down(&ch->sema);
			if(ch->exit_status == -1){
				return TID_ERROR;
			}
			break;
		}
	}
	
	return child_tid;
}

#ifndef VM
/* 이 함수를 `pml4_for_each`에 전달하여 부모의 주소 공간(address space)을 복제합니다.  
   이 기능은 프로젝트 2에서만 사용됩니다. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux; //parent
	void *parent_page = pte;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if(is_kernel_vaddr(va)){
		return true;
	}
	/* 2. Resolve VA from the parent's page map level 4. */
	// parent의 pml4에서 va에 해당되는 페이지 가져오기
	parent_page = pml4_get_page (parent->pml4, va);
	if(parent_page == NULL){
		return false;
	}

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */

	newpage = palloc_get_page(PAL_USER);

	if(newpage == NULL){
		return false;
	}

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */	
	memcpy(newpage, parent_page, (1<<12));
	writable = (*pte & PTE_W) != 0;
	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		palloc_free_page(newpage);
		return false;
	}
	return true;
}
#endif

/* 부모의 실행 컨텍스트를 복사하는 스레드 함수입니다.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct fork_aux * aux_ = (struct fork_aux*)aux;
	struct intr_frame if_;
	struct thread *parent = aux_->parent;
	struct thread *current = thread_current ();
	
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if = aux_->frame;
	bool succ = true;

	// 부모의 레지스터 상태를 그대로 저장
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	if_.R.rax = 0; // 단 자식의 rax값은 0이 되어야 한다.
	// current->tf = if_;

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	
	struct file ** parent_ft = parent->file_table;

	for(int i=0;i<127;i++){
		struct file *pf = parent_ft[i];
		if(pf == NULL){
			current->file_table[i]=NULL;
			continue;
		}
		lock_acquire(&file_lock);
		current->file_table[i] = file_duplicate(pf);
		lock_release(&file_lock);
	}

	process_init ();

	struct child *ci = current->my_self;   /* 내 child 구조체 */

    if (succ) {                     /* ★ fork 완전 성공 */
		ci->exit_status = 0;
        sema_up (&ci->sema);        /* 이제야 부모 깨움  */
        free (aux);                 /* 준비된 인자 해제  */
        do_iret (&if_);             /* 사용자 영역 진입 */
    }
//
error:                              /* 복제 중 하나라도 실패 */
    /* 부모에게 실패(-1) 통보 */
	ci->exit_status = -1;
    ci->is_exit = true;
    sema_up (&ci->sema);
    free (aux);
    thread_exit ();
}

void
init_parse(char *parse[]){
	for(int i=0;i<64;i++){
		parse[i]= NULL;
	}
}

/* 현재 실행 컨텍스트를 f_name으로 전환합니다.  
   실패할 경우 -1을 반환합니다. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;

	/* 스레드 구조체 내의 `intr_frame`을 사용할 수 없습니다.  
	   그 이유는 현재 스레드가 리스케줄될 때,  
	   해당 멤버에 실행 정보를 저장하기 때문입니다. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();
	supplemental_page_table_init(&thread_current()->spt);
 
	char *save_ptr;
	char* parse[64];

	init_parse(parse);

	char *token = strtok_r(file_name, " ", &save_ptr);
	parse[0] = token;
	int argc = 1;

	/* And then load the binary */
	success = load (parse[0], &_if);

	
	if (!success){
		palloc_free_page (file_name);
		file_name = NULL;
		return -1;
	}
		

	while(token != NULL){
		token = strtok_r(NULL, " ", &save_ptr);
		if(token == NULL) break;
		parse[argc++] = token;
	}

	char* argv[argc];
	size_t size;
	for (int j = argc-1;j>=0;j--){
		size = strlen(parse[j])+1;
		_if.rsp -= size;
		argv[j]=_if.rsp;
		memcpy(_if.rsp, parse[j],size);
	}
	
	_if.rsp = _if.rsp & ~0x7;
	
	_if.rsp -= sizeof(char *);
	memset(_if.rsp, 0, sizeof(char *));

	for(int j = argc -1; j>=0 ;j--){
		_if.rsp -= sizeof(char *);
		memcpy(_if.rsp, &argv[j], sizeof(char *));
	}

	_if.R.rsi = _if.rsp;
	_if.R.rdi = argc;
	
	_if.rsp -= sizeof(void *);
	memset(_if.rsp, 0, sizeof(char *));

	palloc_free_page (file_name);
	file_name = NULL;

	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */

	struct thread *cur = thread_current();
	struct list_elem *e;
	if (child_tid == NULL)
		return -1;
	for(e=list_begin(&cur->child_list); e!=list_tail(&cur->child_list);
		e=list_next(e)){
			struct child *c = list_entry(e, struct child, elem);
			if (c->child_tid == child_tid && c->is_waited == false){
				c->is_waited = true;  // add before sema_down
				sema_down(&c->sema);
				int result_status = c->exit_status;
				list_remove(e);
				free(c);
				return result_status;
			}
	}
	return -1;
	
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
    /* TODO: 여기에 코드 작성
     * TODO: 프로세스 종료 메시지 구현 (project2/process_termination.html 참고)
     * TODO: 프로세스 자원 정리를 이곳에 구현하는 것을 권장합니다. */


	struct list_elem *e, *next;
	for(e=list_begin(&curr->child_list);e!=list_end(&curr->child_list);e=next){
		next = list_next(e);
		struct child* ci = list_entry(e, struct child, elem);
		list_remove(e);
		free(ci);

	}

	struct file** ft = curr->file_table;
	for(int i=0;i<127;i++){
		if(ft[i]==NULL){
			continue;
		}
		file_close(ft[i]);
	}

	if (curr->run_file){
		file_close (curr->run_file);
	}
    process_cleanup ();
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* 현재 프로세스의 페이지 디렉터리를 제거하고,  
	   커널 전용 페이지 디렉터리로 전환합니다. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* C여기에서 올바른 순서대로 수행하는 것이 매우 중요합니다.  
		   타이머 인터럽트가 프로세스의 페이지 디렉터리로  
		   다시 전환하지 않도록,  
		   페이지 디렉터리를 전환하기 전에  
		   `cur->pagedir`을 NULL로 설정해야 합니다.  

		   또한, 프로세스의 페이지 디렉터리를 제거하기 전에  
		   기본(base) 페이지 디렉터리를 활성화해야 합니다.  
		   그렇지 않으면 활성 페이지 디렉터리가  
		   이미 해제(그리고 초기화)된 디렉터리가 될 수 있습니다. */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* 다음 스레드에서 사용자 코드를 실행할 수 있도록 CPU를 설정합니다.  
   이 함수는 컨텍스트 스위치가 발생할 때마다 호출됩니다.*/
void
process_activate (struct thread *next) {
	/* 스레드의 페이지 테이블을 활성화합니다. */
	pml4_activate (next->pml4);

	/* 인터럽트를 처리하는 데 사용할 스레드의 커널 스택을 설정합니다. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* 실행 파일 헤더입니다. [ELF1] 1-4부터 1-8까지를 참조하세요.
   ELF 바이너리의 맨 처음에 나타납니다 */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* 현재 스레드에 FILE_NAME으로부터 ELF 실행 파일을 로드합니다.  
   실행 파일의 진입 지점(entry point)은 *RIP에 저장하고,  
   초기 스택 포인터는 *RSP에 저장합니다.  
   성공 시 true를, 실패 시 false를 반환합니다. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* 페이지 디렉터리를 할당하고 활성화합니다. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* 실행 파일을 엽니다. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}
	t->run_file = file;
	file_deny_write(file);

	/* 실행 파일의 헤더를 읽고 검증합니다. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* 프로그램 헤더를 읽습니다. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* 이 세그먼트는 무시합니다. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;	//file_page는 오프셋?
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;		//mem_page는 가상 주소?
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* 일반적인 세그먼트입니다.  
					   	   초기 부분은 디스크에서 읽고, 나머지는 0으로 채웁니다.*/
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* 완전히 0으로 초기화됩니다.  
						   디스크에서 아무 것도 읽지 않습니다. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	success = true;

done:
	/* 로드가 성공했든 실패했든 우리는 여기까지 도달하게 됩니다. */
	return success;
}


/* PHDR이 FILE 내에서 유효하고 로드 가능한 세그먼트를  
   설명하는지 확인하며, 해당하는 경우 true를 반환하고  
   그렇지 않으면 false를 반환합니다. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset과 p_vaddr는 동일한 페이지 오프셋을 가져야 합니다. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset은 FILE 내를 가리켜야 합니다. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz는 p_filesz보다 크거나 같아야 합니다. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* 세그먼트는 비어 있으면 안 됩니다. */
	if (phdr->p_memsz == 0)
		return false;

	/* 가상 메모리 영역의 시작과 끝은 모두  
	   사용자 주소 공간 범위 내에 있어야 합니다. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* 해당 메모리 영역은 커널 가상 주소 공간을 가로질러  
	   "랩어라운드(wrap around)"되어서는 안 됩니다. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* 페이지 0 매핑은 허용하지 않습니다.  
	   페이지 0을 매핑하는 것은 위험할 뿐만 아니라,  
	   만약 이를 허용한다면 사용자 코드가 시스템 콜에  
	   널 포인터(null pointer)를 전달했을 때  
	   `memcpy()` 등의 함수 내에서 발생하는 널 포인터 단언문(assertion)에 의해  
	   커널이 패닉(panic)을 일으킬 가능성이 높아집니다. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* 이 코드 블록은 **오직 프로젝트 2에서만 사용됩니다.**  
   프로젝트 2 전체에서 해당 기능을 구현하고자 한다면,  
   `#ifndef` 매크로 외부에 구현해야 합니다. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* 파일 FILE의 OFS 오프셋부터 시작하여  
   주소 UPAGE에 세그먼트를 로드합니다.  
   총 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리를 다음과 같이 초기화합니다:

   - UPAGE에서 시작하는 READ_BYTES 바이트는  
     FILE의 OFS 오프셋부터 읽어옵니다.

   - UPAGE + READ_BYTES부터 시작하는 ZERO_BYTES 바이트는  
     0으로 초기화합니다.

   이 함수에 의해 초기화된 페이지는  
   WRITABLE이 true일 경우 사용자 프로세스가 쓰기 가능해야 하며,  
   false인 경우 읽기 전용이어야 합니다.

   메모리 할당 오류나 디스크 읽기 오류가 발생하지 않으면 true를 반환하고,  
   그렇지 않으면 false를 반환합니다. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* 이 페이지를 어떻게 채울지 계산합니다.  
	  	   FILE에서 PAGE_READ_BYTES 바이트를 읽고,  
		   마지막 PAGE_ZERO_BYTES 바이트는 0으로 초기화합니다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* 해당 페이지를 프로세스의 주소 공간에 추가합니다. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* 읽은 바이트 수만큼 파일 오프셋을 앞으로 이동시킨다. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* USER_STACK 위치에 0으로 초기화된 페이지를 매핑하여 최소한의 스택을 생성합니다. */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* 사용자 가상 주소 UPAGE에서 커널 가상 주소 KPAGE로의  
   매핑을 페이지 테이블에 추가합니다.  
   WRITABLE이 true이면 사용자 프로세스가 해당 페이지를 수정할 수 있으며,  
   false이면 읽기 전용입니다.  

   UPAGE는 이미 매핑되어 있지 않아야 합니다.  
   KPAGE는 아마도 palloc_get_page()를 통해  
   user pool에서 얻은 페이지여야 합니다.  

   성공 시 true를 반환하며,  
   UPAGE가 이미 매핑되어 있거나 메모리 할당에 실패한 경우 false를 반환합니다. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* 이 지점부터의 코드는 프로젝트 3 이후에 사용됩니다.  
   프로젝트 2에서만 해당 기능을 구현하고자 한다면,  
   위쪽 블록에 구현하세요. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: 파일에서 세그먼트를 로드합니다. */
	struct read_file *read_file_load = (struct read_file *)aux;
	/* TODO: 이 함수는 주소 VA에서 첫 번째 페이지 폴트가 발생했을 때 호출됩니다. */

	file_seek(read_file_load->file, read_file_load->ofs);
	
	if(file_read(read_file_load->file, page->frame->kva, read_file_load->page_read_bytes) != (int)read_file_load->page_read_bytes){
		palloc_free_page(page->frame->kva);
		return false;
	}

	memset(page->frame->kva + read_file_load->page_read_bytes, 0, read_file_load->page_zero_bytes);

	return true;
	/* TODO: 이 함수를 호출할 때 VA(가상 주소)는 접근 가능한 상태입니다. */
}

/* 파일 FILE의 OFS 오프셋부터 시작하여  
   주소 UPAGE에 세그먼트를 로드합니다.  
   총 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리를 다음과 같이 초기화합니다:

   - UPAGE에서 시작하는 READ_BYTES 바이트는  
     FILE의 OFS 오프셋부터 읽어옵니다.

   - UPAGE + READ_BYTES부터 시작하는 ZERO_BYTES 바이트는  
     0으로 초기화합니다.

   이 함수에 의해 초기화된 페이지는  
   WRITABLE이 true인 경우 사용자 프로세스가 쓸 수 있어야 하고,  
   그렇지 않은 경우 읽기 전용이어야 합니다.

   메모리 할당 오류나 디스크 읽기 오류가 발생하지 않으면 true를 반환하고,  
   그렇지 않으면 false를 반환합니다. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* 이 페이지를 어떻게 채울지 계산합니다.  
		   PAGE_READ_BYTES 바이트는 FILE에서 읽고,  
		   마지막 PAGE_ZERO_BYTES 바이트는 0으로 초기화합니다.*/
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;	//파일로 부터 읽을 바이트 수?
		size_t page_zero_bytes = PGSIZE - page_read_bytes;  				//0으로 채워야 할 바이트의 수?

		/* TODO: `lazy_load_segment` 함수에 정보를 전달하기 위해 `aux`를 설정합니다. */
		// 무슨 정보가 들어가야 하는지?
		//file_read에 필요한 정보?

		struct read_file *read_file_load = malloc(sizeof(struct read_file));	//read_file 구조체 할당
		read_file_load->file = file;
		read_file_load->ofs = ofs;
		read_file_load->page_read_bytes = page_read_bytes;
		read_file_load->page_zero_bytes = page_zero_bytes;

		// void *aux = NULL;
		void *aux = (void *)read_file_load;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;

		ofs += page_read_bytes;
	}
	return true;
}

/* USER_STACK 위치에 스택용 PAGE를 생성합니다. 성공 시 true를 반환합니다. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);
	struct thread *curr = thread_current();		//디버깅용

	/* TODO: `stack_bottom` 위치에 스택을 매핑하고, 즉시 해당 페이지를 할당(claim)합니다. */
	//페이지를 초기화 하는데 사용
	if(!vm_alloc_page_with_initializer (VM_ANON | VM_MARKER_0, stack_bottom, true, NULL, NULL)){
		return success;
	}
	if(!vm_claim_page(stack_bottom)){
		return success;
	}
	/* TODO: 성공하면, 해당 값에 따라 `rsp`를 설정합니다.*/
	if_->rsp = USER_STACK;
	/* TODO: 해당 페이지를 스택으로 표시해야 합니다. */
	success = true;
	/* TODO: 여기에 여러분의 코드가 들어갑니다. */

	return success;
}
#endif /* VM */
