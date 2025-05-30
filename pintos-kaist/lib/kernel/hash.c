/* Hash table.

   이 자료구조는 Pintos Project 3의 설명서(Tour of Pintos) 에 자세히 문서화되어 있습니다.
   기본적인 정보는 hash.h 파일을 참고하세요. */

#include "hash.h"
#include "../debug.h"
#include "threads/malloc.h"

#define list_elem_to_hash_elem(LIST_ELEM)                       \
	list_entry(LIST_ELEM, struct hash_elem, list_elem)

static struct list *find_bucket (struct hash *, struct hash_elem *);
static struct hash_elem *find_elem (struct hash *, struct list *,
		struct hash_elem *);
static void insert_elem (struct hash *, struct list *, struct hash_elem *);
static void remove_elem (struct hash *, struct hash_elem *);
static void rehash (struct hash *);

/* 해시 테이블 H를 초기화합니다.
   이때, 해시 값 계산에는 HASH 함수를 사용하고,
   해시 요소 비교에는 LESS 함수를 사용하며,
   AUX는 보조 데이터로 전달됩니다. */
bool
hash_init (struct hash *h, hash_hash_func *hash, hash_less_func *less, void *aux) {
	h->elem_cnt = 0;
	h->bucket_cnt = 4;
	h->buckets = malloc (sizeof *h->buckets * h->bucket_cnt);
	h->hash = hash;
	h->less = less;
	h->aux = aux;

	if (h->buckets != NULL) {
		hash_clear (h, NULL);
		return true;
	} else
		return false;
}

/* 해시 테이블 H의 모든 요소를 제거합니다.

DESTRUCTOR가 null이 아닌 경우, 해시 테이블의 각 요소에 대해 DESTRUCTOR 함수가 호출됩니다.
이 DESTRUCTOR 함수는 필요하다면 해당 해시 요소가 사용하는 메모리를 해제할 수 있습니다.

단, hash_clear()가 실행되는 동안
hash_clear(), hash_destroy(), hash_insert(), hash_replace(), hash_delete()와 같은
해시 테이블을 수정하는 함수들을 호출하는 것은
그것이 DESTRUCTOR 안에서든, 다른 위치에서든 정의되지 않은 동작(undefined behavior) 을 일으킬 수 있습니다. */
void
hash_clear (struct hash *h, hash_action_func *destructor) {
	size_t i;

	for (i = 0; i < h->bucket_cnt; i++) {
		struct list *bucket = &h->buckets[i];

		if (destructor != NULL)
			while (!list_empty (bucket)) {
				struct list_elem *list_elem = list_pop_front (bucket);
				struct hash_elem *hash_elem = list_elem_to_hash_elem (list_elem);
				destructor (hash_elem, h->aux);
			}

		list_init (bucket);
	}

	h->elem_cnt = 0;
}

/* 해시 테이블 H를 완전히 파괴(destroy) 합니다.

   만약 DESTRUCTOR가 null이 아니라면, 해시 테이블에 있는 각 요소에 대해
   먼저 DESTRUCTOR 함수가 호출됩니다.
   이 함수는 적절할 경우, 해당 해시 요소가 사용하는 메모리를 해제할 수 있습니다.

   단, hash_clear()가 실행되는 도중에
   hash_clear(), hash_destroy(), hash_insert(), hash_replace(), hash_delete()
   와 같은 함수를 호출하여 해시 테이블을 수정하는 행위는
   그것이 DESTRUCTOR 내부에서든, 다른 위치에서든
   정의되지 않은 동작(undefined behavior) 을 초래할 수 있습니다. */
void
hash_destroy (struct hash *h, hash_action_func *destructor) {
	if (destructor != NULL)
		hash_clear (h, destructor);
	free (h->buckets);
}

/* NEW 요소를 해시 테이블 H에 삽입합니다.
   만약 동일한 요소가 테이블에 없다면, NEW를 삽입하고 null 포인터를 반환합니다.

   반대로, 이미 동일한 요소가 존재한다면,
   NEW는 삽입하지 않고 기존 요소를 반환합니다. */
struct hash_elem *
hash_insert (struct hash *h, struct hash_elem *new) {
	struct list *bucket = find_bucket (h, new);
	struct hash_elem *old = find_elem (h, bucket, new);

	if (old == NULL)
		insert_elem (h, bucket, new);

	rehash (h);

	return old;
}

/* NEW 요소를 해시 테이블 H에 삽입합니다.
   만약 동일한 요소가 이미 테이블에 존재한다면,
   기존 요소를 NEW로 교체하고, 기존 요소를 반환합니다. */
struct hash_elem *
hash_replace (struct hash *h, struct hash_elem *new) {
	struct list *bucket = find_bucket (h, new);
	struct hash_elem *old = find_elem (h, bucket, new);

	if (old != NULL)
		remove_elem (h, old);
	insert_elem (h, bucket, new);

	rehash (h);

	return old;
}

/* 해시 테이블 H에서 E와 동일한 요소를 찾아 반환합니다.

   만약 동일한 요소가 존재하지 않으면,
   null 포인터를 반환합니다. */
struct hash_elem *
hash_find (struct hash *h, struct hash_elem *e) {
	return find_elem (h, find_bucket (h, e), e);
}

/* 해시 테이블 H에서 E와 동일한 요소를 찾아 제거한 후 반환합니다.
   만약 동일한 요소가 존재하지 않으면,
   null 포인터를 반환합니다.

   해시 테이블의 요소들이 동적으로 할당되었거나,
   또는 동적으로 할당된 자원을 소유하고 있는 경우,
   해당 자원을 해제하는 책임은 호출자에게 있습니다. */
struct hash_elem *
hash_delete (struct hash *h, struct hash_elem *e) {
	struct hash_elem *found = find_elem (h, find_bucket (h, e), e);
	if (found != NULL) {
		remove_elem (h, found);
		rehash (h);
	}
	return found;
}

/* 해시 테이블 H의 각 요소에 대해,
   임의의 순서로 ACTION 함수를 호출합니다.

   단, hash_apply()가 실행되는 동안
   hash_clear(), hash_destroy(), hash_insert(), hash_replace(), hash_delete() 등의 함수를 사용하여
   해시 테이블 H를 수정하면,
   그것이 ACTION 함수 내부이든 외부이든 정의되지 않은 동작(undefined behavior) 을 유발합니다. */
void
hash_apply (struct hash *h, hash_action_func *action) {
	size_t i;

	ASSERT (action != NULL);

	for (i = 0; i < h->bucket_cnt; i++) {
		struct list *bucket = &h->buckets[i];
		struct list_elem *elem, *next;

		for (elem = list_begin (bucket); elem != list_end (bucket); elem = next) {
			next = list_next (elem);
			action (list_elem_to_hash_elem (elem), h->aux);
		}
	}
}

/* 해시 테이블 반복자 초기화와 사용.

   Iteration idiom:

   struct hash_iterator i;

   hash_first (&i, h);
   while (hash_next (&i))
   {
   struct foo *f = hash_entry (hash_cur (&i), struct foo, elem);
   ...do something with f...
   }

   해시 테이블 H를 순회(iteration)하는 도중에
   hash_clear(), hash_destroy(), hash_insert(), hash_replace(), hash_delete()와 같은 함수를 사용하여
   해시 테이블을 수정하면,
   현재 사용 중인 모든 반복자(iterator)는 무효화됩니다.. */
void
hash_first (struct hash_iterator *i, struct hash *h) {
	ASSERT (i != NULL);
	ASSERT (h != NULL);

	i->hash = h;
	i->bucket = i->hash->buckets;
	i->elem = list_elem_to_hash_elem (list_head (i->bucket));
}

/* 반복자 I를 해시 테이블에서 다음 요소로 이동시키고,
   그 요소를 반환합니다.
   더 이상 요소가 없으면 null 포인터를 반환합니다.
   요소들은 임의의 순서로 반환됩니다.

   해시 테이블 H를 순회하는 도중에
   hash_clear(), hash_destroy(), hash_insert(), hash_replace(), hash_delete() 등의 함수를 사용해
   구조를 변경하면,
   현재 사용 중인 모든 반복자는 무효화됩니다. */
struct hash_elem *
hash_next (struct hash_iterator *i) {
	ASSERT (i != NULL);

	i->elem = list_elem_to_hash_elem (list_next (&i->elem->list_elem));
	while (i->elem == list_elem_to_hash_elem (list_end (i->bucket))) {
		if (++i->bucket >= i->hash->buckets + i->hash->bucket_cnt) {
			i->elem = NULL;
			break;
		}
		i->elem = list_elem_to_hash_elem (list_begin (i->bucket));
	}

	return i->elem;
}

/* 해시 테이블 순회 중에, 현재 요소를 반환합니다.
   만약 순회가 끝났다면 null 포인터를 반환합니다.

   단, hash_first()를 호출한 직후 hash_next()를 호출하기 전까지
   이 함수를 사용하면 정의되지 않은 동작(undefined behavior) 이 발생할 수 있습니다. */
struct hash_elem *
hash_cur (struct hash_iterator *i) {
	return i->elem;
}

/* 해시 테이블 H에 포함된 요소의 개수를 반환합니다. */
size_t
hash_size (struct hash *h) {
	return h->elem_cnt;
}

/* 해시 테이블 H에 요소가 하나도 없으면 true를 반환하고,
   그렇지 않으면 false를 반환합니다. */
bool
hash_empty (struct hash *h) {
	return h->elem_cnt == 0;
}

/* 32비트 워드 크기용 Fowler–Noll–Vo(FNV) 해시 상수입니다. */
#define FNV_64_PRIME 0x00000100000001B3UL
#define FNV_64_BASIS 0xcbf29ce484222325UL

/* BUF에 있는 SIZE 바이트 크기의 데이터에 대한 해시 값을 반환합니다. */
uint64_t
hash_bytes (const void *buf_, size_t size) {
	/* Fowler-Noll-Vo 32-bit hash, for bytes. */
	const unsigned char *buf = buf_;
	uint64_t hash;

	ASSERT (buf != NULL);

	hash = FNV_64_BASIS;
	while (size-- > 0)
		hash = (hash * FNV_64_PRIME) ^ *buf++;

	return hash;
}

/* 문자열 S에 대한 해시 값을 반환합니다. */
uint64_t
hash_string (const char *s_) {
	const unsigned char *s = (const unsigned char *) s_;
	uint64_t hash;

	ASSERT (s != NULL);

	hash = FNV_64_BASIS;
	while (*s != '\0')
		hash = (hash * FNV_64_PRIME) ^ *s++;

	return hash;
}

/* 정수 I에 대한 해시 값을 반환합니다. */
uint64_t
hash_int (int i) {
	return hash_bytes (&i, sizeof i);
}

/* 해시 요소 E가 속하는 해시 테이블 H의 버킷(bucket)을 반환합니다. */
static struct list *
find_bucket (struct hash *h, struct hash_elem *e) {
	size_t bucket_idx = h->hash (e, h->aux) & (h->bucket_cnt - 1);
	return &h->buckets[bucket_idx];
}

/* 해시 테이블 H에서 BUCKET을 검색하여,
   E와 동일한 해시 요소를 찾습니다.

   찾으면 해당 요소를 반환하고,
   찾지 못하면 null 포인터를 반환합니다. */
static struct hash_elem *
find_elem (struct hash *h, struct list *bucket, struct hash_elem *e) {
	struct list_elem *i;

	for (i = list_begin (bucket); i != list_end (bucket); i = list_next (i)) {
		struct hash_elem *hi = list_elem_to_hash_elem (i);
		if (!h->less (hi, e, h->aux) && !h->less (e, hi, h->aux))
			return hi;
	}
	return NULL;
}

/* X의 가장 낮은 자리(최하위 비트)에 설정된 1을 0으로 바꾼 값을 반환합니다. */
static inline size_t
turn_off_least_1bit (size_t x) {
	return x & (x - 1);
}

/* X가 2의 거듭제곱이면 true를 반환하고,
   그렇지 않으면 false를 반환합니다. */
static inline size_t
is_power_of_2 (size_t x) {
	return x != 0 && turn_off_least_1bit (x) == 0;
}

/* 버킷 하나당 평균 요소 수 */
#define MIN_ELEMS_PER_BUCKET  1 /* Elems/bucket < 1: reduce # of buckets. */
#define BEST_ELEMS_PER_BUCKET 2 /* Ideal elems/bucket. */
#define MAX_ELEMS_PER_BUCKET  4 /* Elems/bucket > 4: increase # of buckets. */

/* 해시 테이블 H의 버킷 수를 이상적인 수준에 맞게 조정합니다.

   이 함수는 메모리 부족(out-of-memory) 으로 인해 실패할 수 있지만,
   그 경우 단지 해시 접근이 덜 효율적일 뿐이며,
   프로그램 실행은 계속될 수 있습니다. */
static void
rehash (struct hash *h) {
	size_t old_bucket_cnt, new_bucket_cnt;
	struct list *new_buckets, *old_buckets;
	size_t i;

	ASSERT (h != NULL);

	/* Save old bucket info for later use. */
	old_buckets = h->buckets;
	old_bucket_cnt = h->bucket_cnt;

	/* 현재 사용할 버킷 수를 계산합니다.

       이상적으로는 요소 BEST_ELEMS_PER_BUCKET 개당 하나의 버킷이 있도록 설정합니다.
	   단, 버킷 수는 최소 4개 이상이어야 하며,
	   반드시 2의 거듭제곱 수여야 합니다. */
	new_bucket_cnt = h->elem_cnt / BEST_ELEMS_PER_BUCKET;
	if (new_bucket_cnt < 4)
		new_bucket_cnt = 4;
	while (!is_power_of_2 (new_bucket_cnt))
		new_bucket_cnt = turn_off_least_1bit (new_bucket_cnt);

	/* 버킷 수가 변경되지 않는다면 아무 작업도 하지 않습니다. */
	if (new_bucket_cnt == old_bucket_cnt)
		return;

	/* Allocate new buckets and initialize them as empty. */
	new_buckets = malloc (sizeof *new_buckets * new_bucket_cnt);
	if (new_buckets == NULL) {
		/* Allocation failed.  This means that use of the hash table will
		   be less efficient.  However, it is still usable, so
		   there's no reason for it to be an error. */
		return;
	}
	for (i = 0; i < new_bucket_cnt; i++)
		list_init (&new_buckets[i]);

	/* Install new bucket info. */
	h->buckets = new_buckets;
	h->bucket_cnt = new_bucket_cnt;

	/* Move each old element into the appropriate new bucket. */
	for (i = 0; i < old_bucket_cnt; i++) {
		struct list *old_bucket;
		struct list_elem *elem, *next;

		old_bucket = &old_buckets[i];
		for (elem = list_begin (old_bucket);
				elem != list_end (old_bucket); elem = next) {
			struct list *new_bucket
				= find_bucket (h, list_elem_to_hash_elem (elem));
			next = list_next (elem);
			list_remove (elem);
			list_push_front (new_bucket, elem);
		}
	}

	free (old_buckets);
}

/* E를 해시 테이블 H의 BUCKET에 삽입합니다. */
static void
insert_elem (struct hash *h, struct list *bucket, struct hash_elem *e) {
	h->elem_cnt++;
	list_push_front (bucket, &e->list_elem);
}

/* 해시 테이블 H에서 요소 E를 제거합니다. */
static void
remove_elem (struct hash *h, struct hash_elem *e) {
	h->elem_cnt--;
	list_remove (&e->list_elem);
}

