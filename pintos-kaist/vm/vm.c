/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "include/lib/kernel/hash.h"
#include "include/threads/vaddr.h"
#include "vm/uninit.h"
#include "threads/mmu.h"


static bool hash_less (const struct hash_elem *a,
						const struct hash_elem *b,void *aux);
static unsigned
page_hash(const struct hash_elem *e, void *aux UNUSED);
/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
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
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
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

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {
	/*uninit 타입이 입력으로 들어오면 안됨. 이 함수는 특정 타입(file, anon)으로
	페이지를 생성하고 초기화하기 위한 함수이기 때문에*/
	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *page = malloc(sizeof(struct page));
		if(page == NULL)
			goto err;
		bool (*page_initializer) (struct page *, enum vm_type, void *kva);
		switch (VM_TYPE(type)) {
			case VM_ANON:
				page_initializer = anon_initializer;
				break;
			
			case VM_FILE:
				page_initializer = file_backed_initializer;
				break;
			
			default:
				free(page);
				goto err;
		}

		uninit_new(page, upage, init, type,
							aux, page_initializer);
		page->writable = writable;

		/* TODO: Insert the page into the spt. */ 
		if (!spt_insert_page(spt, page)){
			free(page);
			goto err;
		}
		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	// struct page key;
	struct page *page = malloc(sizeof(struct page));
	/* TODO: Fill this function. */
	// key.va = pg_round_down(va);
	page->va = pg_round_down(va);
	struct hash_elem *e = hash_find(spt->spt_hash, &page->hash_elem);
	free(page);
	if (e != NULL) {
		struct page *page;
		page = hash_entry(e, struct page, hash_elem);
		return page;
	}
	return NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	bool succ = false;
	/* TODO: Fill this function. */
	if(spt_find_page(spt, page->va) == NULL) {
		struct hash_elem *h = hash_insert(spt->spt_hash, &page->hash_elem);
		if (h == NULL)
			succ = true;
	}	
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	/*물리 주소(kva)를 할당*/
	void *kva = palloc_get_page(PAL_USER);
	if (kva == NULL) {
		palloc_free_page(kva);
		PANIC("todo");
	}
	/*프레임 구조체 생성(힙에 할당)*/
	frame = malloc(sizeof(struct frame));
	if (frame == NULL) {
		PANIC("vm_get_frame: malloc failed");
	}

	frame->kva = kva;
	frame->page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {

}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
	bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if (addr == NULL || is_kernel_vaddr(addr)) {
		return false;
	}

	page = spt_find_page(spt, addr);
	if(page == NULL)
		return false;

	//쓰기 접근인데 read_only면 실패
	if(write && !page->writable)
		return false;

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
/*va로 pml4 세팅*/
bool
vm_claim_page (void *va UNUSED) {
	/* TODO: Fill this function */
	// 1. 커널 주소인지 검사
	if (!is_user_vaddr(va))
		return false;
	/*2. spt에 해당 va를 갖는 페이지가 존재하는지 검사.
	없으면 오류처리, 있으면 pml4세팅*/
	struct thread *curr = thread_current();
	struct page *page;
	page = spt_find_page(&curr->spt, pg_round_down(va));
	if(page == NULL){
		return false;
	}else
		return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
/*page로 pml4 세팅*/
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread *cur = thread_current();
	//bool succ = false;
	if(pml4_get_page(cur->pml4, page->va) == NULL){
		return pml4_set_page(cur->pml4, page->va, frame->kva, page->writable);
		//succ = true;
	}
	// return false;
	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(spt->spt_hash,page_hash,hash_less,NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	struct hash_iterator *i;

}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}

//hash_hash 함수
static unsigned
page_hash(const struct hash_elem *e, void *aux UNUSED) {
    struct page *p = hash_entry(e, struct page, hash_elem);  // hash_elem → struct page
    return hash_bytes(pg_round_down(p->va), sizeof(p->va));  // va를 기준으로 해시값 생성
}

//hash 비교 함수
static bool hash_less (const struct hash_elem *a,const struct hash_elem *b,void *aux){
	struct page *pa = hash_entry(a, struct page, hash_elem);
    struct page *pb = hash_entry(b, struct page, hash_elem);
	return pg_round_down(pa->va) < pg_round_down(pb->va);
}