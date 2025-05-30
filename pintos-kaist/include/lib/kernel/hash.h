#ifndef __LIB_KERNEL_HASH_H
#define __LIB_KERNEL_HASH_H

/* Hash table.
이 자료구조는 Pintos Project 3의 설명서(Tour of Pintos) 에 자세히 문서화되어 있습니다.

이 구조는 체이닝(chaining) 기법을 사용하는 표준 해시 테이블입니다.
테이블에서 특정 요소를 찾기 위해, 해당 요소의 데이터를 기반으로 해시 함수를 계산하여
이중 연결 리스트 배열의 인덱스로 사용한 뒤, 그 리스트를 선형 탐색(linear search) 합니다.

이 체인 리스트는 동적 할당을 사용하지 않습니다. 대신,
해시 테이블에 들어갈 수 있는 모든 구조체는 내부에 struct hash_elem 멤버를 포함해야 합니다.
모든 해시 함수는 이 hash_elem 구조체를 기준으로 동작합니다.

hash_entry 매크로를 이용하면 struct hash_elem에서 다시 해당 구조체 객체로 변환할 수 있습니다.
이 방식은 연결 리스트(list) 구현에서도 동일하게 사용됩니다.
자세한 내용은 lib/kernel/list.h를 참고하세요. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "list.h"

/* Hash element. */
struct hash_elem {
	struct list_elem list_elem;
};

/* 포인터 HASH_ELEM이 가리키는 해시 요소(hash element) 를,
   이 요소가 포함되어 있는 외부 구조체의 포인터로 변환합니다.
   외부 구조체의 이름 STRUCT와, 그 안에 포함된 해시 요소의 멤버 이름 MEMBER를 함께 제공해야 합니다.
   사용 예시는 이 파일의 맨 위에 있는 주석을 참고하세요. */
#define hash_entry(HASH_ELEM, STRUCT, MEMBER)                   \
	((STRUCT *) ((uint8_t *) &(HASH_ELEM)->list_elem        \
		- offsetof (STRUCT, MEMBER.list_elem)))

/* 해시 요소 E에 대해, 보조 데이터 AUX를 기반으로
   해시 값을 계산하고 반환합니다. */
typedef uint64_t hash_hash_func (const struct hash_elem *e, void *aux);

/* 두 해시 요소 A와 B의 값을, 보조 데이터 AUX를 기준으로 비교합니다.
   A가 B보다 작으면 true를 반환하고,
   그렇지 않으면 false를 반환합니다. */
typedef bool hash_less_func (const struct hash_elem *a,
		const struct hash_elem *b,
		void *aux);

/* 해시 요소 E에 대해, 보조 데이터 AUX를 이용하여
   특정 연산을 수행합니다. */
typedef void hash_action_func (struct hash_elem *e, void *aux);

/* Hash table. */
struct hash {
	size_t elem_cnt;            /* 테이블에 있는 요소(원소)의 수 */
	size_t bucket_cnt;          /* 버킷(bucket)의 개수이며, 2의 거듭제곱 수입니다. */
	struct list *buckets;       /* bucket_cnt 개수만큼의 리스트로 구성된 배열입니다. */
	hash_hash_func *hash;       /* 해시 함수. */
	hash_less_func *less;       /* 비교 함수. */
	void *aux;                  /* hash 함수와 less 함수에서 사용하는 보조 데이터. */
};

/* 해시 테이블 반복자. */
struct hash_iterator {
	struct hash *hash;          /* The hash table. */
	struct list *bucket;        /* 현재 버킷. */
	struct hash_elem *elem;     /* 현재 버킷에 있는 현재 해시 요소. */
};

/* 기본 동작 흐름. */
bool hash_init (struct hash *, hash_hash_func *, hash_less_func *, void *aux);
void hash_clear (struct hash *, hash_action_func *);
void hash_destroy (struct hash *, hash_action_func *);

/* 검색, 삽입, 삭제. */
struct hash_elem *hash_insert (struct hash *, struct hash_elem *);
struct hash_elem *hash_replace (struct hash *, struct hash_elem *);
struct hash_elem *hash_find (struct hash *, struct hash_elem *);
struct hash_elem *hash_delete (struct hash *, struct hash_elem *);

/* 반복 또는 순회. */
void hash_apply (struct hash *, hash_action_func *);
void hash_first (struct hash_iterator *, struct hash *);
struct hash_elem *hash_next (struct hash_iterator *);
struct hash_elem *hash_cur (struct hash_iterator *);

/* 정보. */
size_t hash_size (struct hash *);
bool hash_empty (struct hash *);

/* 해시 함수 예제. */
uint64_t hash_bytes (const void *, size_t);
uint64_t hash_string (const char *);
uint64_t hash_int (int);

#endif /* lib/kernel/hash.h */
