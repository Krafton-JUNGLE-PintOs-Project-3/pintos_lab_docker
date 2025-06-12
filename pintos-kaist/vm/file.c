/* file.c: 메모리를 기반으로 한 파일 객체(mmap된 객체)의 구현 */
#include "filesys/file.h"
#include <string.h>
#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"


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

/* 파일 가상 메모리의 초기화*/
void
vm_file_init (void) {

}

/* 파일 기반 페이지를 초기화하라 
   파일 기반 페이지를 위한 초기화 함수*/
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* 핸들러 설정 */
	memset(&page->uninit, 0, sizeof(struct uninit_page));
	
	//lazy_load 와 비슷하게 작성하면 될거 같은 느낌?
	page->operations = &file_ops;
	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

//file의 오프셋 바이트부터 length 바이트만큼을 프로세스의 가상주소공간의 주소 addr에 매핑 한다.
/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	
	void *return_addr = addr;

	size_t read_bytes = (length > file_length(file)) ? file_length(file) : length; //file 크기가 더 작은 경우도 있기때문에
	size_t zero_bytes = PGSIZE - (read_bytes % PGSIZE);


	while(read_bytes > 0 || zero_bytes > 0){

		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct read_file *read_file_load = malloc(sizeof(struct read_file));	//read_file 구조체 할당
		read_file_load->file = file;
		read_file_load->ofs = offset;
		read_file_load->page_read_bytes = page_read_bytes;
		read_file_load->page_zero_bytes = page_zero_bytes;

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

	// struct thread *curr = thread_current();
	// struct page *page;

	// while((page = spt_find_page(&curr->spt, addr))){
	// 	if(page != NULL){
	// 		destroy(page);
	// 	}
	// 	addr += PGSIZE;
	// }
}
