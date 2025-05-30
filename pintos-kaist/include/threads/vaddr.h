#ifndef THREADS_VADDR_H
#define THREADS_VADDR_H

#include <debug.h>
#include <stdint.h>
#include <stdbool.h>

#include "threads/loader.h"

/* 가상 주소를 다루기 위한 함수와 매크로들입니다.
   x86 하드웨어 페이지 테이블에 특화된 함수와 매크로는 pte.h 파일을 참고하세요. */

#define BITMASK(SHIFT, CNT) (((1ul << (CNT)) - 1) << (SHIFT))

/* Page offset (bits 0:12). */
#define PGSHIFT 0                          /* 오프셋 비트의 시작 인덱스. */
#define PGBITS  12                         /* 오프셋 비트의 개수. */
#define PGSIZE  (1 << PGBITS)              /* 한 페이지의 바이트 수. */
#define PGMASK  BITMASK(PGSHIFT, PGBITS)   /* 페이지 오프셋 비트 범위 (0~12비트). */

/* 페이지 내 오프셋 */
#define pg_ofs(va) ((uint64_t) (va) & PGMASK)

#define pg_no(va) ((uint64_t) (va) >> PGBITS)

/* 가장 가까운 페이지 경계로 올림(round up).
   어떤 주소가 페이지 중간에 있다면, 
   그 주소를 다음 페이지의 시작 주소로 올리는 것을 의미*/
#define pg_round_up(va) ((void *) (((uint64_t) (va) + PGSIZE - 1) & ~PGMASK))

/* 가장 가까운 페이지 경계로 내림(round down). 
   어떤 주소가 페이지 중간에 있다면, 
   그 주소를 포함하는 현재 페이지의 시작 주소로 내리는 것을 의미*/
#define pg_round_down(va) (void *) ((uint64_t) (va) & ~PGMASK)

/* 커널 가상 주소 시작점 */
#define KERN_BASE LOADER_KERN_BASE

/* 유저 스택 시작 주소 */
#define USER_STACK 0x47480000

/* VADDR가 유저 가상 주소(user virtual address)일 경우 true를 반환. */
#define is_user_vaddr(vaddr) (!is_kernel_vaddr((vaddr)))

/* Returns true if VADDR is a kernel virtual address. */
#define is_kernel_vaddr(vaddr) ((uint64_t)(vaddr) >= KERN_BASE)

// FIXME: add checking
/* Returns kernel virtual address at which physical address PADDR
 *  is mapped. */
#define ptov(paddr) ((void *) (((uint64_t) paddr) + KERN_BASE))

/* Returns physical address at which kernel virtual address VADDR
 * is mapped. */
#define vtop(vaddr) \
({ \
	ASSERT(is_kernel_vaddr(vaddr)); \
	((uint64_t) (vaddr) - (uint64_t) KERN_BASE);\
})

#endif /* threads/vaddr.h */
