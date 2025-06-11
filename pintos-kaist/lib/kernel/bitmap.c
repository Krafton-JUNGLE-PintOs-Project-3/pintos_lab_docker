#include "bitmap.h"
#include <debug.h>
#include <limits.h>
#include <round.h>
#include <stdio.h>
#include "threads/malloc.h"
#ifdef FILESYS
#include "filesys/file.h"
#endif

/* Element type.

   이 타입은 int보다 크거나 같은 폭을 가진 부호 없는 정수형(unsigned integer type) 이어야 합니다.

   이 타입의 각 비트는 비트맵에서의 한 비트를 나타냅니다.
   즉, 어떤 요소의 비트 0이 비트맵의 K번째 비트를 나타낸다면,
   그 다음 비트 1은 K+1번째 비트, 비트 2는 K+2번째 비트를 나타내는 식입니다. */
typedef unsigned long elem_type;

/* 요소 하나당 포함된 비트(bit)의 수. */
#define ELEM_BITS (sizeof (elem_type) * CHAR_BIT)

/* 외부에서 보면, 비트맵(bitmap)은 비트들의 배열입니다.
내부적으로는, 비트 배열을 흉내 내기 위해 elem_type(위에서 정의됨)의 배열로 구성되어 있습니다. */
struct bitmap {
	size_t bit_cnt;     /* Number of bits. */
	elem_type *bits;    /* Elements that represent bits. */
};

/* BIT_IDX로 번호가 매겨진 비트를 포함하고 있는 요소의 인덱스를 반환합니다. */
static inline size_t
elem_idx (size_t bit_idx) {
	return bit_idx / ELEM_BITS;
}

/* BIT_IDX에 해당하는 비트만 켜져 있는 elem_type 값을 반환합니다. */
static inline elem_type
bit_mask (size_t bit_idx) {
	return (elem_type) 1 << (bit_idx % ELEM_BITS);
}

/* BIT_CNT 비트를 저장하기 위해 필요한 요소(요소의 개수)를 반환합니다. */
static inline size_t
elem_cnt (size_t bit_cnt) {
	return DIV_ROUND_UP (bit_cnt, ELEM_BITS);
}

/* BIT_CNT 비트를 저장하기 위해 필요한 바이트 수를 반환합니다. */
static inline size_t
byte_cnt (size_t bit_cnt) {
	return sizeof (elem_type) * elem_cnt (bit_cnt);
}

/* B의 비트맵에서 마지막 요소에 실제로 사용된 비트들만 1로 설정되고, 
나머지 비트는 0으로 설정된 비트 마스크를 반환합니다. */
static inline elem_type
last_mask (const struct bitmap *b) {
	int last_bits = b->bit_cnt % ELEM_BITS;
	return last_bits ? ((elem_type) 1 << last_bits) - 1 : (elem_type) -1;
}

/* Creation and destruction. */

/* B를 BIT_CNT 비트 크기의 비트맵으로 초기화하고,
모든 비트를 false로 설정합니다.
메모리 할당에 성공하면 true를, 실패하면 false를 반환합니다. */
struct bitmap *
bitmap_create (size_t bit_cnt) {
	struct bitmap *b = malloc (sizeof *b);
	if (b != NULL) {
		b->bit_cnt = bit_cnt;
		b->bits = malloc (byte_cnt (bit_cnt));
		if (b->bits != NULL || bit_cnt == 0) {
			bitmap_set_all (b, false);
			return b;
		}
		free (b);
	}
	return NULL;
}

/* BIT_CNT 비트를 가지는 비트맵을 생성하고 반환합니다. 
이때 비트맵은 사전에 할당된 BLOCK이라는 메모리 공간(BLOCK_SIZE 바이트)을 사용합니다. 
BLOCK_SIZE는 최소한 bitmap_needed_bytes(BIT_CNT)보다 커야 합니다. */
struct bitmap *
bitmap_create_in_buf (size_t bit_cnt, void *block, size_t block_size UNUSED) {
	struct bitmap *b = block;

	ASSERT (block_size >= bitmap_buf_size (bit_cnt));

	b->bit_cnt = bit_cnt;
	b->bits = (elem_type *) (b + 1);
	bitmap_set_all (b, false);
	return b;
}

/* Returns the number of bytes required to accomodate a bitmap
   with BIT_CNT bits (for use with bitmap_create_in_buf()). */
size_t
bitmap_buf_size (size_t bit_cnt) {
	return sizeof (struct bitmap) + byte_cnt (bit_cnt);
}

/* 비트맵 B를 파괴(destroy)하고, 그에 할당된 메모리 공간을 해제합니다.
단, bitmap_create_preallocated() 함수로 생성된 비트맵에는 이 함수를 사용하면 안 됩니다. */
void
bitmap_destroy (struct bitmap *b) {
	if (b != NULL) {
		free (b->bits);
		free (b);
	}
}

/* Bitmap size. */

/* 비트맵 B에 포함된 비트의 개수를 반환합니다. */
size_t
bitmap_size (const struct bitmap *b) {
	return b->bit_cnt;
}

/* Setting and testing single bits. */

/* IDX 번호에 해당하는 비트를 비트맵 B에서 **VALUE 값(true 또는 false)**로 원자적으로 설정합니다.
(※ 원자적 설정이란: 중간에 다른 스레드나 인터럽트에 의해 방해받지 않고 한 번에 완료된다는 의미입니다.) */
void
bitmap_set (struct bitmap *b, size_t idx, bool value) {
	ASSERT (b != NULL);
	ASSERT (idx < b->bit_cnt);
	if (value)
		bitmap_mark (b, idx);
	else
		bitmap_reset (b, idx);
}

/* 비트맵 B에서 번호가 BIT_IDX인 비트를 true로 원자적으로 설정합니다.
   즉, 해당 비트를 1로 바꾸는 작업이 중단 없이 한 번에 수행됩니다. */
void
bitmap_mark (struct bitmap *b, size_t bit_idx) {
	size_t idx = elem_idx (bit_idx);
	elem_type mask = bit_mask (bit_idx);

	/* This is equivalent to `b->bits[idx] |= mask' except that it
	   is guaranteed to be atomic on a uniprocessor machine.  See
	   the description of the OR instruction in [IA32-v2b]. */
	asm ("lock orq %1, %0" : "=m" (b->bits[idx]) : "r" (mask) : "cc");
}

/* 비트맵 B에서 번호가 BIT_IDX인 비트를 false로 원자적으로 설정합니다.
   즉, 해당 비트를 0으로 바꾸는 작업이 중단 없이 한 번에 수행됩니다. */
void
bitmap_reset (struct bitmap *b, size_t bit_idx) {
	size_t idx = elem_idx (bit_idx);
	elem_type mask = bit_mask (bit_idx);

	/* This is equivalent to `b->bits[idx] &= ~mask' except that it
	   is guaranteed to be atomic on a uniprocessor machine.  See
	   the description of the AND instruction in [IA32-v2a]. */
	asm ("lock andq %1, %0" : "=m" (b->bits[idx]) : "r" (~mask) : "cc");
}

/* 비트맵 B에서 번호가 IDX인 비트를 **원자적으로 토글(toggle)**합니다.
즉, 해당 비트가 **true(1)**이면 **false(0)**로, **false(0)**이면 **true(1)**로 바꿉니다.
이 작업은 중단 없이 한 번에 수행되어 경쟁 상태 없이 안전하게 처리됩니다. */
void
bitmap_flip (struct bitmap *b, size_t bit_idx) {
	size_t idx = elem_idx (bit_idx);
	elem_type mask = bit_mask (bit_idx);

	/* This is equivalent to `b->bits[idx] ^= mask' except that it
	   is guaranteed to be atomic on a uniprocessor machine.  See
	   the description of the XOR instruction in [IA32-v2b]. */
	asm ("lock xorq %1, %0" : "=m" (b->bits[idx]) : "r" (mask) : "cc");
}

/* Returns the value of the bit numbered IDX in B. */
bool
bitmap_test (const struct bitmap *b, size_t idx) {
	ASSERT (b != NULL);
	ASSERT (idx < b->bit_cnt);
	return (b->bits[elem_idx (idx)] & bit_mask (idx)) != 0;
}

/* Setting and testing multiple bits. */

/* 비트맵 B에 있는 모든 비트를 VALUE로 설정합니다. */
void
bitmap_set_all (struct bitmap *b, bool value) {
	ASSERT (b != NULL);

	bitmap_set_multiple (b, 0, bitmap_size (b), value);
}

/* Sets the CNT bits starting at START in B to VALUE. */
void
bitmap_set_multiple (struct bitmap *b, size_t start, size_t cnt, bool value) {
	size_t i;

	ASSERT (b != NULL);
	ASSERT (start <= b->bit_cnt);
	ASSERT (start + cnt <= b->bit_cnt);

	for (i = 0; i < cnt; i++)
		bitmap_set (b, start + i, value);
}

/* 비트맵 B에서 START부터 START + CNT 사이 (START 이상, START + CNT 미만) 구간에
설정된 비트들 중에서, **VALUE와 같은 값(true 또는 false)**을 가진 비트의 개수를 반환합니다. */
size_t
bitmap_count (const struct bitmap *b, size_t start, size_t cnt, bool value) {
	size_t i, value_cnt;

	ASSERT (b != NULL);
	ASSERT (start <= b->bit_cnt);
	ASSERT (start + cnt <= b->bit_cnt);

	value_cnt = 0;
	for (i = 0; i < cnt; i++)
		if (bitmap_test (b, start + i) == value)
			value_cnt++;
	return value_cnt;
}

/* 비트맵 B에서 START 이상 START + CNT 미만 구간 내에
**VALUE 값(true 또는 false)**을 가진 비트가 하나라도 존재하면 true,
그렇지 않으면 false를 반환합니다. */
bool
bitmap_contains (const struct bitmap *b, size_t start, size_t cnt, bool value) {
	size_t i;

	ASSERT (b != NULL);
	ASSERT (start <= b->bit_cnt);
	ASSERT (start + cnt <= b->bit_cnt);

	for (i = 0; i < cnt; i++)
		if (bitmap_test (b, start + i) == value)
			return true;
	return false;
}

/* 비트맵 B에서 START 이상, START + CNT 미만 범위에
   true로 설정된 비트가 하나라도 있으면 true,
   그렇지 않으면 false를 반환합니다.*/
bool
bitmap_any (const struct bitmap *b, size_t start, size_t cnt) {
	return bitmap_contains (b, start, cnt, true);
}

/* Returns true if no bits in B between START and START + CNT,
   exclusive, are set to true, and false otherwise.*/
bool
bitmap_none (const struct bitmap *b, size_t start, size_t cnt) {
	return !bitmap_contains (b, start, cnt, true);
}

/* Returns true if every bit in B between START and START + CNT,
   exclusive, is set to true, and false otherwise. */
bool
bitmap_all (const struct bitmap *b, size_t start, size_t cnt) {
	return !bitmap_contains (b, start, cnt, false);
}

/* Finding set or unset bits. */

/* CNT개의 연속된 비트들이 모두 VALUE로 설정된 첫 번째 그룹을
   비트맵 B에서 START 위치 이후부터 찾아 해당 시작 인덱스를 반환합니다.
   그런 그룹이 없다면, BITMAP_ERROR를 반환합니다. */
size_t
bitmap_scan (const struct bitmap *b, size_t start, size_t cnt, bool value) {
	ASSERT (b != NULL);
	ASSERT (start <= b->bit_cnt);

	if (cnt <= b->bit_cnt) {
		size_t last = b->bit_cnt - cnt;
		size_t i;
		for (i = start; i <= last; i++)
			if (!bitmap_contains (b, i, cnt, !value))
				return i;
	}
	return BITMAP_ERROR;
}

/* 비트맵 B에서 START 위치 이후부터 CNT개의 연속된 비트가
   모두 VALUE로 설정된 첫 번째 그룹을 찾아,
   그 비트들을 전부 !VALUE(반대 값)로 뒤집고,
   그 그룹의 첫 번째 비트의 인덱스를 반환합니다.
   해당하는 그룹이 없으면 BITMAP_ERROR를 반환합니다.
   CNT가 0이면 0을 반환합니다.
   비트를 설정하는 동작은 원자적으로 수행되지만,
   비트를 테스트(확인)하는 동작과 설정하는 동작은 원자적으로 수행되지 않습니다. */
size_t
bitmap_scan_and_flip (struct bitmap *b, size_t start, size_t cnt, bool value) {
	size_t idx = bitmap_scan (b, start, cnt, value);
	if (idx != BITMAP_ERROR)
		bitmap_set_multiple (b, idx, cnt, !value);
	return idx;
}

/* File input and output. */

#ifdef FILESYS
/* Returns the number of bytes needed to store B in a file. */
size_t
bitmap_file_size (const struct bitmap *b) {
	return byte_cnt (b->bit_cnt);
}

/* Reads B from FILE.  Returns true if successful, false
   otherwise. */
bool
bitmap_read (struct bitmap *b, struct file *file) {
	bool success = true;
	if (b->bit_cnt > 0) {
		off_t size = byte_cnt (b->bit_cnt);
		success = file_read_at (file, b->bits, size, 0) == size;
		b->bits[elem_cnt (b->bit_cnt) - 1] &= last_mask (b);
	}
	return success;
}

/* Writes B to FILE.  Return true if successful, false
   otherwise. */
bool
bitmap_write (const struct bitmap *b, struct file *file) {
	off_t size = byte_cnt (b->bit_cnt);
	return file_write_at (file, b->bits, size, 0) == size;
}
#endif /* FILESYS */

/* Debugging. */

/* Dumps the contents of B to the console as hexadecimal. */
void
bitmap_dump (const struct bitmap *b) {
	hex_dump (0, b->bits, byte_cnt (b->bit_cnt), false);
}

