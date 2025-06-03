#ifndef VM_VM_H
#define VM_VM_H
#include <stdbool.h>
#include "threads/palloc.h"

enum vm_type {
	/* 페이지가 초기화되지 않았습니다. */
	VM_UNINIT = 0,
	/* 파일과 관련 없는 페이지, 즉 **익명 페이지(anonymous page)**입니다. */
	VM_ANON = 1,
	/* 파일과 관련된 페이지, 즉 **파일 매핑 페이지(file-backed page)**입니다. */
	VM_FILE = 2,
	/* 페이지 캐시를 보관하는 페이지, 즉 페이지 캐시용 페이지입니다.(Project 4 기준) */
	VM_PAGE_CACHE = 3,

	/* 상태를 저장하기 위한 비트 플래그 */

	/* 정보를 저장하기 위한 보조 비트 플래그 마커입니다.
	   값을 int 타입의 크기 안에 담을 수 있는 한,
	   마커를 더 추가해도 괜찮습니다. */
	VM_MARKER_0 = (1 << 3),
	VM_MARKER_1 = (1 << 4),

	/* 이 값을 초과하지 마세요. */
	VM_MARKER_END = (1 << 31),
};

#include "vm/uninit.h"
#include "vm/anon.h"
#include "vm/file.h"
#include "lib/kernel/hash.h"
#ifdef EFILESYS
#include "filesys/page_cache.h"
#endif

struct page_operations;
struct thread;
//vm_alloc_page_with_initializer 함수 작성시
//해당 메크로를 사용하면 편리??
#define VM_TYPE(type) ((type) & 7)		

/* "page"를 표현하는 구조체입니다.
   이 구조체는 일종의 "부모 클래스" 역할을 하며,
   네 가지 "자식 클래스" 인 uninit_page, file_page, anon_page, 
   그리고 페이지 캐시(page cache, project4) 가 이를 상속하거나 확장하는 방식입니다.

   이 구조체에 정의된 기존 멤버는 절대 삭제하거나 수정하지 마세요. */
struct page {
	const struct page_operations *operations;
	void *va;              /* 사용자 공간 상의 주소 */
	struct frame *frame;   /* 프레임에 대한 역참조(Back Reference) */

	/* 사용자가 직접 작성한 구현 */
	struct hash_elem hash_elem;	//해시테이블에 넣기위한 elem
	bool writable;				//쓰기 권한
	

	/* 타입별 데이터는 union(공용체)에 결합되어 있습니다.
	   각 함수는 현재 사용 중인 union 타입을 자동으로 감지합니다. */
	union {
		struct uninit_page uninit;
		struct anon_page anon;
		struct file_page file;
#ifdef EFILESYS
		struct page_cache page_cache;
#endif
	};
};

/* 프레임의 표현 방식 */
struct frame {
	void *kva;				//커널 가상 주소
	struct page *page;		//페이지 구조체를 담기 위한 멤버
};

/* 페이지 동작을 위한 함수 테이블입니다.
   이는 C 언어에서 “인터페이스”를 구현하는 한 가지 방법입니다.

   구조체의 멤버에 “메서드 테이블”을 저장해 두고,
   필요할 때마다 해당 함수를 호출하면 됩니다. */
struct page_operations {
	bool (*swap_in) (struct page *, void *);
	bool (*swap_out) (struct page *);
	void (*destroy) (struct page *);
	enum vm_type type;
};

#define swap_in(page, v) (page)->operations->swap_in ((page), v)
#define swap_out(page) (page)->operations->swap_out (page)
#define destroy(page) \
	if ((page)->operations->destroy) (page)->operations->destroy (page)

/* 현재 프로세스의 메모리 공간을 표현하는 구조체입니다.
   이 구조체의 설계 방식에 대해 특정한 형식을 강요하지는 않습니다.

   어떻게 설계할지는 전적으로 여러분에게 달려 있습니다. */
struct supplemental_page_table {
	struct hash spt_hash;//해시테이블 넣고
};

#include "threads/thread.h"
void supplemental_page_table_init (struct supplemental_page_table *spt);
bool supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src);
void supplemental_page_table_kill (struct supplemental_page_table *spt);
struct page *spt_find_page (struct supplemental_page_table *spt,
		void *va);
bool spt_insert_page (struct supplemental_page_table *spt, struct page *page);
void spt_remove_page (struct supplemental_page_table *spt, struct page *page);

void vm_init (void);
bool vm_try_handle_fault (struct intr_frame *f, void *addr, bool user,
		bool write, bool not_present);

#define vm_alloc_page(type, upage, writable) \
	vm_alloc_page_with_initializer ((type), (upage), (writable), NULL, NULL)
bool vm_alloc_page_with_initializer (enum vm_type type, void *upage,
		bool writable, vm_initializer *init, void *aux);
void vm_dealloc_page (struct page *page);
bool vm_claim_page (void *va);
enum vm_type page_get_type (struct page *page);

#endif  /* VM_VM_H */