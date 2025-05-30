/* uninit.c: Implementation of uninitialized page.
 *
 * 모든 페이지는 초기화되지 않은(uninit) 페이지로 생성된다.
   첫 번째 페이지 폴트(page fault)가 발생하면,
   핸들러 체인(handler chain)이 uninit_initialize(page->operations.swap_in)을 호출한다.
   uninit_initialize 함수는 페이지 객체를 초기화하여 해당 페이지를 
   특정한 페이지 객체(anon, file, page_cache)로 변환(transmute)하며,
   vm_alloc_page_with_initializer 함수에서 전달된 초기화 콜백(callback)을 호출한다.
 * */

#include "vm/vm.h"
#include "vm/uninit.h"

static bool uninit_initialize (struct page *page, void *kva);
static void uninit_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations uninit_ops = {
	.swap_in = uninit_initialize,
	.swap_out = NULL,
	.destroy = uninit_destroy,
	.type = VM_UNINIT,
};

/* DO NOT MODIFY this function */
void
uninit_new (struct page *page, void *va, vm_initializer *init,
		enum vm_type type, void *aux,
		bool (*initializer)(struct page *, enum vm_type, void *)) {
	ASSERT (page != NULL);

	*page = (struct page) {
		.operations = &uninit_ops,
		.va = va,
		.frame = NULL, /* no frame for now */
		.uninit = (struct uninit_page) {
			.init = init,
			.type = type,
			.aux = aux,
			.page_initializer = initializer,
		}
	};
}

/* 첫 번째 페이지 폴트 발생 시 페이지를 초기화한다 */
static bool
uninit_initialize (struct page *page, void *kva) {
	struct uninit_page *uninit = &page->uninit;

	/* 먼저 데이터를 불러온 뒤, page_initialize 함수가 해당 값을 덮어쓸 수 있다 */
	vm_initializer *init = uninit->init;
	void *aux = uninit->aux;

	/* TODO: 이 함수를 수정해야 할 수도 있습니다 */
	return uninit->page_initializer (page, uninit->type, kva) &&
		(init ? init (page, aux) : true);
}

/* uninit_page가 보유하고 있는 자원을 해제한다.
   대부분의 페이지는 다른 페이지 객체로 변환되지만,
   프로세스가 종료될 때까지 한 번도 참조되지 않은 uninit 페이지가 남아 있을 수 있다.
   페이지(PAGE) 자체는 호출자(caller)가 해제한다. */
static void
uninit_destroy (struct page *page) {
	struct uninit_page *uninit UNUSED = &page->uninit;
	/* TODO: Fill this function.
	 * TODO: 수행할 작업이 없다면 그냥 반환하세요. */
}
