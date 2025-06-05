#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/syscall.h"
#include "intrinsic.h"

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/* 사용자 프로그램에 의해 발생할 수 있는 인터럽트에 대한 핸들러들을 등록합니다.

   실제 유닉스 계열 OS에서는 이러한 인터럽트 대부분이  
   [SV-386] 3-24와 3-25에서 설명된 것처럼,  
   신호(signal)의 형태로 사용자 프로세스에 전달됩니다.  
   하지만 우리는 신호를 구현하지 않기 때문에,  
   대신 해당 인터럽트가 발생하면 사용자 프로세스를 종료시킵니다.

   단, 페이지 폴트는 예외입니다.  
   여기서는 다른 예외들과 동일하게 처리되지만,  
   가상 메모리를 구현하려면 이 방식을 변경해야 합니다.

   각 예외에 대한 설명은 [IA32-v3a]의 5.15절  
   "Exception and Interrupt Reference"를 참고하세요. */
void
exception_init (void) {
	/* 이러한 예외들은 사용자 프로그램에 의해 명시적으로 발생할 수 있습니다.  
	   예를 들어 `INT`, `INT3`, `INTO`, `BOUND` 명령어를 통해 발생할 수 있습니다.  
	   따라서, 이러한 예외들은 DPL(Descriptor Privilege Level)을 3으로 설정하여  
	   사용자 프로그램이 이러한 명령어를 통해 예외를 호출할 수 있도록 합니다. */
	intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
	intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
	intr_register_int (5, 3, INTR_ON, kill,
			"#BR BOUND Range Exceeded Exception");

	/* 이러한 예외들은 DPL이 0으로 설정되어 있어,  
	   사용자 프로세스가 INT 명령어를 통해 직접 호출할 수 없습니다.  
	   하지만 여전히 간접적으로 발생할 수는 있습니다.  
	   예를 들어, 0으로 나눌 경우 #DE(0 나누기 예외)가 발생할 수 있습니다. */
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

	/* 대부분의 예외는 인터럽트를 활성화한 상태에서도 처리할 수 있습니다.  
	   하지만 페이지 폴트의 경우에는 인터럽트를 비활성화해야 합니다.  
	   그 이유는 폴트가 발생한 주소가 CR2 레지스터에 저장되기 때문이며,  
	   해당 값을 반드시 보존해야 하기 때문입니다. */
	intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* 예외 발생 통계를 출력합니다. */
void
exception_print_stats (void) {
	printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* 사용자 프로세스에 의해 (아마도) 발생한 예외를 처리하는 핸들러입니다. */
static void
kill (struct intr_frame *f) {
	/* 이 인터럽트는 (아마도) 사용자 프로세스에 의해 발생한 것입니다.  
	   예를 들어, 프로세스가 매핑되지 않은 가상 메모리에 접근하려고 했을 수 있습니다  
	   (페이지 폴트의 예). 현재는 단순히 해당 사용자 프로세스를 종료시키지만,  
	   나중에는 커널에서 페이지 폴트를 처리하도록 구현할 예정입니다.  

	   실제 유닉스 계열 운영체제에서는 대부분의 예외를  
	   시그널(signal)의 형태로 사용자 프로세스에 다시 전달하지만,  
	   우리는 시그널을 구현하지 않습니다. */

	/* The interrupt frame's code segment value tells us where the
	   exception originated. */
	switch (f->cs) {
		case SEL_UCSEG:
			/* User's code segment, so it's a user exception, as we
			   expected.  Kill the user process.  */
			printf ("%s: dying due to interrupt %#04llx (%s).\n",
					thread_name (), f->vec_no, intr_name (f->vec_no));
			intr_dump_frame (f);
			thread_exit ();

		case SEL_KCSEG:
			/* Kernel's code segment, which indicates a kernel bug.
			   Kernel code shouldn't throw exceptions.  (Page faults
			   may cause kernel exceptions--but they shouldn't arrive
			   here.)  Panic the kernel to make the point.  */
			intr_dump_frame (f);
			PANIC ("Kernel bug - unexpected interrupt in kernel");

		default:
			/* Some other code segment?  Shouldn't happen.  Panic the
			   kernel. */
			printf ("Interrupt %#04llx (%s) in unknown segment %04x\n",
					f->vec_no, intr_name (f->vec_no), f->cs);
			thread_exit ();
	}
}

/* 페이지 폴트 핸들러입니다.
  이 코드는 가상 메모리를 구현하기 위해 반드시 완성되어야 하는 뼈대(skeleton) 코드입니다.
  프로젝트 2의 일부 해법에서는 이 코드를 수정해야 할 수도 있습니다.

  진입 시, 폴트가 발생한 주소는 CR2(Control Register 2) 레지스터에 저장되어 있으며,
  폴트에 대한 정보는 exception.h에 정의된 PF_* 매크로 형식으로 F 구조체의 error_code 멤버에 저장됩니다.
  아래의 예제 코드는 그 정보를 어떻게 파싱하는지 보여줍니다.

  이 내용에 대한 더 많은 정보는
  [IA32-v3a] 문서의 5.15절, **"Exception and Interrupt Reference"**의
  "Interrupt 14--Page Fault Exception (#PF)" 부분에서 확인할 수 있습니다. */
static void
page_fault (struct intr_frame *f) {
	bool not_present;  /* True: 존재하지 않는(not-present) 페이지, false: 읽기 전용(r/o) 페이지에 쓰기 시도 중. */
	bool write;        /* True: 접근은 쓰기(write)였고, false: 접근은 읽기(read)였다. */
	bool user;         /* True: 사용자(user)에 의한 접근입니다. false라면, 커널(kernel)에 의한 접근입니다. */
	void *fault_addr;  /* Fault address. */

	/* 페이지 폴트를 발생시킨 가상 주소(faulting address)를 구합니다.
	   이 주소는 코드(명령어) 또는 데이터일 수 있습니다.
	   이 주소는 반드시 페이지 폴트를 유발한 명령어의 주소는 아닙니다 (그건 f->rip에 있습니다). */

	fault_addr = (void *) rcr2();

	/* 인터럽트를 다시 활성화합니다. 
	   (인터럽트를 비활성화했던 이유는 CR2 레지스터를 읽기 전에 그 값이 변경되지 않도록 보장하기 위함이었습니다.) */
	intr_enable ();


	/* 원인을 파악하라. */
	not_present = (f->error_code & PF_P) == 0;
	write = (f->error_code & PF_W) != 0;
	user = (f->error_code & PF_U) != 0;

#ifdef VM
	/* For project 3 and later. */
	// //user 가상 영역을 넘어가는 요청일 경우
	// if((!not_present && write) || (fault_addr < 0x400000 || fault_addr >= USER_STACK)){
	// 	sys_exit(-1);
	// }

	if (vm_try_handle_fault (f, fault_addr, user, write, not_present))
		return;
#endif

	/* Count page faults. */
	page_fault_cnt++;

	sys_exit(-1);

		/* If the fault is true fault, show info and exit. */
	printf ("Page fault at %p: %s error %s page in %s context.\n",
			fault_addr,
			not_present ? "not present" : "rights violation",
			write ? "writing" : "reading",
			user ? "user" : "kernel");
	kill (f);
	
}

