/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

#include "threads/thread.h"

static unsigned page_hash(const struct hash_elem *e, void *aux UNUSED);
static bool hash_less (const struct hash_elem *a,const struct hash_elem *b,void *aux);
/* 가상 메모리 서브시스템을 초기화합니다.
   이를 위해 각 서브시스템의 초기화 코드들을 호출합니다. */
void
vm_init (void) {
	vm_anon_init (); 
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	//vm_init시에 필요한 부분?
	//init.c main에서 호출된다.
}

/* 페이지의 타입을 가져옵니다.
   이 함수는 페이지가 초기화된 이후에 해당 페이지의 타입을 알고 싶을 때 유용합니다.
   이 함수는 이미 완전히 구현되어 있습니다. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* 보조 함수들 */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* 초기화 함수와 함께 대기 중인(pending) 페이지 객체를 생성합니다.
   페이지를 생성하고 싶다면 직접 생성하지 말고,
   반드시 이 함수나 vm_alloc_page를 통해 생성하세요. */
// 처음 파일을 load 하게 되면 인자로 VM_ANON, 0x400000, writable, lazy_load_segment(아직은 비워진), aux(아직은 비워진)가 들어온다.
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)			//VM_TYPE이 VM_UNINIT이 아니여야 함?

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* 해당 사용자 페이지(upage)가 이미 사용 중인지 확인합니다. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: 페이지를 생성하고, VM 타입에 따라 적절한 초기화 함수(initializer)를 가져옵니다.*/
		struct page *page = malloc(sizeof(struct page)); //페이지 생성?
		if(page == NULL) return false;
		//생성 성공시
		page->va = upage;	//upage는 가상 주소?
		page->writable = writable;
		/* TODO: 그런 다음 uninit_new를 호출하여 "uninit" 페이지 구조체를 생성합니다. */
		
		//type에 맞는 initializer_operations을 설정해주어야 한다?
		switch(type){
			case VM_ANON:
				uninit_new(page, upage, init, type, aux, anon_initializer);
				break;
			case VM_FILE:
				uninit_new(page, upage, init, type, aux, file_backed_initializer);
				break;
			default:
				return false;
			break;
		}
		// uninit_new(page, upage, init, type, aux, page->operations);
		/* TODO: uninit_new를 호출한 이후에 필요한 필드를 수정해야 합니다. */
		// 필드를 수정?

		/* TODO: 해당 페이지를 보조 페이지 테이블(SPT) 에 삽입합니다. */
		if(spt_insert_page(spt, page)){
			return true;
		}
		free(page);
	}

err:
	return false;
}

/* SPT에서 가상 주소(VA) 를 찾아 해당 페이지를 반환합니다.
   오류가 발생하면 NULL을 반환합니다. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page;
	/* TODO: 이 함수를 구현하세요. */
	page->va = va; //탐색용 page에 va 넣고

	struct hash_elem *e = hash_find(&spt->spt_hash, &page->hash_elem); //hash find안의 bucket find에서 해싱해줌
	if (e != NULL){
		return hash_entry(e, struct page, hash_elem);
	}
	return NULL;
	// return page;
}

/* 검증 과정을 포함하여 PAGE를 보조 페이지 테이블(SPT) 에 삽입합니다. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: 이 함수를 구현하세요. */
	if(hash_insert(&spt->spt_hash, &page->hash_elem) == NULL){
		succ = true;
	}

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* 교체(evict)될 struct frame을 가져옵니다. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: 페이지 교체 정책(eviction policy)은 여러분이 자유롭게 선택할 수 있습니다. */

	return victim;
}

/* 하나의 페이지를 제거(evict)하고, 해당하는 프레임을 반환합니다.
   오류가 발생한 경우에는 NULL을 반환합니다.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: 대상(victim) 페이지를 스왑 아웃하고, 제외된(evicted) 프레임을 반환합니다. */

	return NULL;
}

/* palloc()을 호출하여 프레임을 할당합니다.
   만약 사용 가능한 페이지가 없다면, 
   기존 페이지를 교체(evict) 하여 프레임을 확보해 반환합니다.

   이 함수는 항상 유효한 주소를 반환해야 합니다.
   즉, 사용자 풀 메모리가 가득 찬 경우에도, 
   프레임을 강제로 비워서라도 사용 가능한 메모리 공간을 확보합니다.*/

static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: 이 함수를 구현하세요. */

	void *kva = palloc_get_page(PAL_USER);		//USER_PAL로 커널 가상 주소 할당
	if(kva == NULL){
		PANIC("todo");				//페이지 할당 불가시 아직 미구현
	} 
	frame = malloc(sizeof(struct frame));		//frame 메모리 할당

	frame->kva = kva;							//커널 가상 주소 저장
	frame->page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* 스택 영역을 확장하는 작업 */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* 쓰기 보호된 페이지에서 발생한 페이지 폴트(fault)를 처리합니다. */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* 성공하면 true를 반환합니다. */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: 페이지 폴트(fault)를 검증합니다. */
	/* TODO: 여기에 여러분의 코드를 작성하세요. */

	return vm_do_claim_page (page);
}

/* Free the page.
 * 이 함수는 수정하지 마세요. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* 가상 주소(VA)에 할당된 페이지를 클레임(claim)합니다. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: 이 함수를 구현하세요. */
	page = spt_find_page(&thread_current()->spt, va);
	if(page == NULL){
		return false;
	}

	return vm_do_claim_page (page);
}

/* 해당 페이지(PAGE)를 클레임(claim) 하고,
   MMU(Memory Management Unit)를 설정합니다. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();	//frame을 하나 할당

	/* Set links */
	frame->page = page;			//서로 연결되어 있는 구조?
	page->frame = frame;

	/* TODO: 페이지의 가상 주소(VA)를 프레임의 물리 주소(PA)에 매핑하도록
    		 페이지 테이블 항목을 삽입합니다. */
	if(pml4_get_page(thread_current()->pml4, page->va) == NULL){//va에 대해 해당하는 물리페이지가 pml4에 매핑이 안되어있으면
		if(pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable))return false;
		//page->writable 권한설정을 구현하면 그 값으로
	}	
	return true; 
	// return swap_in (page, frame->kva);	//page_fault 핸들링시 필요한 부분
}

/* 새로운 보조 페이지 테이블(Supplemental Page Table, SPT)을 초기화합니다. */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->spt_hash, page_hash, hash_less, NULL);
	
}

/* 보조 페이지 테이블(SPT)을 원본(src)에서 대상(dst)으로 복사합니다. */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* 보충 페이지 테이블에서 리소스 보류 해제 */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: 스레드가 보유한 모든 supplemental_page_table을 파괴합니다.
	 * TODO: 수정된 내용을 모두 저장소에 다시 기록합니다. */
}


//hash

//hash_hash 함수
static unsigned
page_hash(const struct hash_elem *e, void *aux UNUSED) {
    struct page *p = hash_entry(e, struct page, hash_elem);  // hash_elem → struct page
    return hash_bytes(&p->va, sizeof(p->va));  // va를 기준으로 해시값 생성
}

//hash 비교 함수
static bool hash_less (const struct hash_elem *a,const struct hash_elem *b,void *aux){
	struct page *pa = hash_entry(a, struct page, hash_elem);
    struct page *pb = hash_entry(b, struct page, hash_elem);
	return pa->va < pb->va;
}