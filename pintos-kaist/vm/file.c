/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include <string.h>
#include <stdlib.h>
static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;
	
	struct file_page *file_page = &page->file;
	struct lazy_load_info *aux = (struct lazy_load_info *)page->uninit.aux;
	file_page->file = aux->file;
	file_page->offset = aux->offset;
	file_page->read_bytes = aux->read_bytes;
	file_page->zero_bytes = aux->zero_bytes;
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	// struct file_page *file_page UNUSED = &page->file;
		struct file_page *file_page = &page->file;
		if (page == NULL)return false;
		off_t offset = file_page->offset;
		size_t page_read_bytes = file_page->read_bytes;
		size_t page_zero_bytes = file_page->zero_bytes;
		
		lock_acquire(&file_lock);
		if (file_read_at (file_page->file, kva, page_read_bytes, offset) != (int) page_read_bytes){
				lock_release(&file_lock);
				return false;
		}
		lock_release(&file_lock);
        memset (kva + page_read_bytes, 0, page_zero_bytes);

        return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	// struct file_page *file_page UNUSED = &page->file;
	struct file_page *file_page = &page->file;
	struct thread *curr = thread_current ();
	
	if (pml4_is_dirty (curr->pml4, page->va)) {
		// lock_acquire(&file_lock);
		file_write_at (file_page->file, page->frame->kva, file_page->read_bytes, file_page->offset);
		// lock_release(&file_lock);
		pml4_set_dirty (curr->pml4, page->va, false);
	}
	page->frame = NULL;
	pml4_clear_page (curr->pml4, page->va);
	

	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {

    // pml4_clear_page(thread_current()->pml4, page->va);
	struct file_page *file_page = &page->file;
	if (page->frame != NULL) {
		
		if (pml4_is_dirty (thread_current()->pml4, page->va)) {
			// lock_acquire(&filesys_lock);
			file_write_at (file_page->file, page->frame->kva,
							file_page->read_bytes, file_page->offset);
			// lock_release(&filesys_lock);
			pml4_set_dirty (thread_current()->pml4, page->va, false);
			
		}
		pml4_clear_page (thread_current()->pml4, page->va);
		palloc_free_page (page->frame->kva);
		free (page->frame);
		page->frame = NULL;
		
	}
		
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	void *return_addr = addr;

	// size_t read_bytes;
	size_t file_bytes = file_length (file);
    size_t read_bytes = length < file_bytes ? length : file_bytes;
    size_t zero_bytes = PGSIZE - (read_bytes % PGSIZE);
	// size_t read_bytes = length;
	// size_t zero_bytes = 0;

	struct thread *curr = thread_current();
	struct page *check = spt_find_page(&curr->spt, addr);	
	if (check != NULL) return NULL; // 중복 매핑 방지	

	while(read_bytes > 0 || zero_bytes > 0){

		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;


		struct lazy_load_info *read_file_load = malloc(sizeof(struct lazy_load_info));	//read_file 구조체 할당
		read_file_load->file = file;
		read_file_load->offset = offset;
		read_file_load->read_bytes = page_read_bytes;
		read_file_load->zero_bytes = page_zero_bytes;

		void *aux = (void *)read_file_load;

		if(!vm_alloc_page_with_initializer (VM_FILE, addr, writable, lazy_load_segment, aux)){
			free(read_file_load);
			return NULL;
		}

		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;

		offset += page_read_bytes;
	}
	return return_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {

	struct thread *curr = thread_current();
	struct page *page;

	while((page = spt_find_page(&curr->spt, addr))){
		if(page != NULL){
			destroy(page);
			spt_remove_page(&curr->spt, page);
		}
		addr += PGSIZE;
	}
}