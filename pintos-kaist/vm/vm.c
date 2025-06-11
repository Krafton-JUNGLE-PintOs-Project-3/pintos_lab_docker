/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "hash.h"
#include <string.h>
static unsigned page_hash(const struct hash_elem *e, void *aux UNUSED);
static bool hash_less (const struct hash_elem *a,const struct hash_elem *b,void *aux);
void spt_destructor(struct hash_elem *e, void* aux);
struct list frame_table;
struct lock hash_lock;
/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
	list_init(&frame_table);
	lock_init(&hash_lock);
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
		vm_initializer *init, void *aux) {// load_segment에서 lazy_load_segment 함수 init에 집어넣음.

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *temp_page = (struct page *)malloc(sizeof(struct page));//va 만들어주고.
		if (temp_page == NULL) goto err;
		
		bool (*initializer[4])(struct page *page, enum vm_type type, void *kva);
		initializer[0] = NULL;
		initializer[1] = anon_initializer;
		initializer[2] = file_backed_initializer;
		initializer[3] = NULL;

		uninit_new(temp_page,upage,init,type,aux,initializer[VM_TYPE(type)]);
		temp_page->page_writable = writable;
		/* TODO: Insert the page into the spt. */
		if(spt_insert_page(spt,temp_page))return true;
		free(temp_page);
	}
	else goto err;
	return true;
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page page;
	/* TODO: Fill this function. */
	page.va = pg_round_down(va); // 탐색용 page에 va 넣고
	struct hash_elem *e = hash_find(&spt->spt_hash, &page.hash_elem);//hash find안의 bucket find에서 해싱해줌
	// free(&page);
	if (e != NULL)
		return hash_entry(e, struct page, hash_elem);
	return NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	lock_acquire(&hash_lock);
	if(hash_insert(&spt->spt_hash,&page->hash_elem)==NULL){//hash_insert는 성공하면 old가 중복값이 없다고 판단하여 old(NULL)을 반환함. 즉 성공하면 NULL을 반환.
		succ=true;
	}
	lock_release(&hash_lock);
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	hash_delete (&spt->spt_hash, &page->hash_elem);
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */
	struct list_elem *e = list_pop_front(&frame_table);
	victim = list_entry(e, struct frame, frame_elem);
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	if (victim == NULL) return NULL;
	struct page *page = victim->page;

	if (page == NULL) return NULL;
    if (!swap_out(page)) return NULL;

	return victim; 
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {

	void *kva = palloc_get_page (PAL_USER);
    struct frame *frame;
	if (kva == NULL) {
		frame = vm_evict_frame ();     /* Reuse an evicted frame. */
		if (frame == NULL)
			PANIC ("vm_get_frame: eviction failed");
			/* The victim already has a kernel page. */
		kva = frame->kva;
	} else {
		frame = malloc (sizeof *frame);

		if (frame == NULL)
			PANIC ("vm_get_frame: malloc failed");
	}
	frame->kva = kva;      /* Kernel virtual address */
	frame->page = NULL;
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	struct thread *curr = thread_current();
	void* stack_bottom = curr->stack_bottom;
	addr = pg_round_down(addr);
	while (addr < stack_bottom){
		stack_bottom -= PGSIZE;
		if(vm_alloc_page_with_initializer (VM_ANON | VM_MARKER_0, stack_bottom, true, NULL, NULL))
			curr->stack_bottom = stack_bottom;
		else break;
		
	}
	
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if (addr == NULL || is_kernel_vaddr(addr)){
		return false;
	} 	

	if(not_present){
		if(addr >= (f->rsp - 8) && (addr <= thread_current ()->stack_bottom) && addr >= (USER_STACK - 0x1000000)){
			vm_stack_growth(addr);
		}
		page = spt_find_page(spt, addr);
		if (!page || (write && !page->page_writable)){
			return false;
		}	
		return vm_do_claim_page(page);
	}
    return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	page = spt_find_page(&thread_current()->spt,va);//spt 테이블에서 페이지 정보 찾고,
	if(page == NULL){
		return false;
	}
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if(pml4_get_page(thread_current()->pml4,page->va)==NULL){//va에 대해 해당하는 물리페이지가 pml4에 매핑이 안되어있으면.
		if(!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->page_writable))return false;
	}

	bool ok = swap_in (page, frame->kva);
	list_push_back(&frame_table, &frame->frame_elem);
	return ok;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->spt_hash,page_hash,hash_less,NULL);
	
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	struct hash_elem *e;
	struct hash_iterator hash_iter;
	bool succ = true;
	hash_first(&hash_iter,&src->spt_hash);
	while (hash_next(&hash_iter)) {
		e = hash_cur(&hash_iter);
        struct page *src_page = hash_entry(e, struct page, hash_elem);
		void *upage = src_page->va;
        enum vm_type type = src_page->operations->type;
        bool writable = src_page->page_writable;
		struct page *dst_page;

        switch (type){
            case VM_UNINIT:{

				if(!vm_alloc_page_with_initializer(src_page->uninit.type, upage, writable, src_page->uninit.init, src_page->uninit.aux)) succ = false;
				if (src_page->frame){
					if(!vm_claim_page(upage))succ = false;
					dst_page = spt_find_page(dst, src_page->va);
                	memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
				}

			}
				
            break;
			case VM_FILE:{
				struct lazy_load_info *copy = malloc(sizeof(struct lazy_load_info));
				copy->file = src_page->file.file;
				copy->offset = src_page->file.offset;
				copy->read_bytes = src_page->file.read_bytes;
				copy->zero_bytes = src_page->file.zero_bytes;

				if(!vm_alloc_page_with_initializer(type, upage, writable, NULL, copy)) {
					free(copy);  // 실패 시 리소스 해제
					succ = false;
				}
                if(!vm_claim_page(upage))succ = false;
                dst_page = spt_find_page(dst, src_page->va);
                memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
			}
			break;
			default:{
				if(!vm_alloc_page_with_initializer(type, upage, writable, NULL, NULL)) succ = false;
                if(!vm_claim_page(upage))succ = false;
                dst_page = spt_find_page(dst, src_page->va);
                memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);

			}
				
			break;
        }
    }
	return succ;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	struct hash_iterator hash_iter;
	struct hash_elem *e;

	lock_acquire(&hash_lock);	
	hash_destroy(&spt->spt_hash, spt_destructor);
	lock_release(&hash_lock);
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

void spt_destructor(struct hash_elem *e, void* aux){
    const struct page *p = hash_entry(e, struct page, hash_elem);
    destroy(p);
	free(p);
}