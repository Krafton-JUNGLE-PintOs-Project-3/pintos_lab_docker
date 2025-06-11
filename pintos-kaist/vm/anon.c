/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include <bitmap.h>
#include "threads/vaddr.h"
#define SECTOR_PER_PAGE (PGSIZE / DISK_SECTOR_SIZE)
static struct bitmap *swap_table;
static struct lock swap_lock;


/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	lock_init(&swap_lock);
	swap_disk = disk_get(1,1);
	swap_table = bitmap_create (disk_size(swap_disk) / SECTOR_PER_PAGE);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	memset(&page->uninit, 0, sizeof(struct uninit_page));
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->swap_index = BITMAP_ERROR;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	lock_acquire(&swap_lock);
	size_t page_no = anon_page->swap_index ;
	if(anon_page->swap_index == BITMAP_ERROR || bitmap_test(swap_table, page_no) == false){
		lock_release(&swap_lock);
		return false;	
	} 

    for (int i = 0; i < SECTOR_PER_PAGE; i++) {
        disk_read(swap_disk, page_no * SECTOR_PER_PAGE + i, kva + DISK_SECTOR_SIZE * i);//한 섹터씩 읽어옴
    }

    bitmap_set(swap_table, page_no, false);

	anon_page->swap_index = BITMAP_ERROR;
	lock_release(&swap_lock);

    return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;


	lock_acquire(&swap_lock);
    // 할당가능한 비트 찾기.
	size_t page_no = bitmap_scan_and_flip(swap_table, 0, 1, false);
	if (page_no == BITMAP_ERROR) {
		lock_release(&swap_lock);
        return false;
    }
 	for (int i = 0; i < SECTOR_PER_PAGE; ++i) {
        disk_write(swap_disk, (page_no * SECTOR_PER_PAGE) + i, page->va + (DISK_SECTOR_SIZE * i));
    }
	bitmap_set(swap_table, page_no, true); //테이블 채웠다고 체크, pml4 클리어
	lock_release(&swap_lock);
	anon_page->swap_index = page_no;
	page->frame->page = NULL;
	page->frame = NULL;
    pml4_clear_page(thread_current()->pml4, page->va);
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	 if (anon_page->swap_index != BITMAP_ERROR){
		lock_acquire(&swap_lock);
		bitmap_reset(swap_table, anon_page->swap_index);
		lock_release(&swap_lock);
	 }

    // if (page->frame) {
	// 	lock_acquire(&swap_lock);
    //     list_remove(&page->frame->frame_elem);
	// 	lock_release(&swap_lock);
    //     page->frame->page = NULL;
    //     free(page->frame);
    //     page->frame = NULL;
    // }
}
	// if (page->frame != NULL) {
    //     palloc_free_page(page->frame->kva);
    // 	free(page->frame);
    // }
	
