/* 약 1MB의 무작위 데이터를 생성한 후, 이를 16개의 청크로 나눕니다.
   각 청크는 별도의 하위 프로세스가 정렬하며, 이 하위 프로세스들은 병렬로 실행됩니다.
   그 후, 이 청크들을 병합하고 결과가 올바른지 확인합니다. */

#include "tests/vm/parallel-merge.h"
#include <stdio.h>
#include <syscall.h>
#include "tests/arc4.h"
#include "tests/lib.h"
#include "tests/main.h"

#define CHUNK_SIZE (128 * 1024)                  //131072??
#define CHUNK_CNT 8                             /* Number of chunks. */
#define DATA_SIZE (CHUNK_CNT * CHUNK_SIZE)      /* Buffer size. */  //1048576??

unsigned char buf1[DATA_SIZE], buf2[DATA_SIZE];
size_t histogram[256];

/* buf1을 무작위 데이터로 초기화한 다음,
   그 안에 포함된 각 값이 몇 번씩 나타나는지를 계산합니다. */
static void
init (void)
{
  struct arc4 arc4;
  size_t i;

  msg ("init");

  arc4_init (&arc4, "foobar", 6);
  arc4_crypt (&arc4, buf1, sizeof buf1);
  for (i = 0; i < sizeof buf1; i++)
    histogram[buf1[i]]++;
}

/* buf1의 각 청크를 SUBPROCESS를 사용해 정렬하며,
  이 SUBPROCESS는 EXIT_STATUS 값을 반환할 것으로 예상됩니다. */
static void
sort_chunks (const char *subprocess, int exit_status)
{
  pid_t children[CHUNK_CNT];
  size_t i;

  for (i = 0; i < CHUNK_CNT; i++)
    {
      char fn[128];
      char cmd[128];
      int handle;

      msg ("sort chunk %zu", i);

      /* Write this chunk to a file. */
      snprintf (fn, sizeof fn, "buf%zu", i);
      create (fn, CHUNK_SIZE);
      quiet = true;
      CHECK ((handle = open (fn)) > 1, "open \"%s\"", fn);
      write (handle, buf1 + CHUNK_SIZE * i, CHUNK_SIZE);
      close (handle);

      /* Sort with subprocess. */
      snprintf (cmd, sizeof cmd, "%s %s", subprocess, fn);
      children[i] = fork (subprocess);
      if (children[i] == 0)
        CHECK ((children[i] = exec (cmd)) != -1, "exec \"%s\"", cmd);
      quiet = false;
    }

  for (i = 0; i < CHUNK_CNT; i++)
    {
      char fn[128];
      int handle;

      CHECK (wait (children[i]) == exit_status, "wait for child %zu", i);

      /* Read chunk back from file. */
      quiet = true;
      snprintf (fn, sizeof fn, "buf%zu", i);
      CHECK ((handle = open (fn)) > 1, "open \"%s\"", fn);
      read (handle, buf1 + CHUNK_SIZE * i, CHUNK_SIZE);
      close (handle);
      quiet = false;
    }
}

/* buf1에 있는 정렬된 조각들을 병합하여 완전히 정렬된 buf2를 만듭니다. */
static void
merge (void)
{
  unsigned char *mp[CHUNK_CNT];
  size_t mp_left;
  unsigned char *op;
  size_t i;

  msg ("merge");

  /* Initialize merge pointers. */
  mp_left = CHUNK_CNT;
  for (i = 0; i < CHUNK_CNT; i++)
    mp[i] = buf1 + CHUNK_SIZE * i;

  /* Merge. */
  op = buf2;
  while (mp_left > 0)
    {
      /* Find smallest value. */
      size_t min = 0;
      for (i = 1; i < mp_left; i++)
        if (*mp[i] < *mp[min])
          min = i;

      /* Append value to buf2. */
      *op++ = *mp[min];

      /* 병합 포인터를 앞으로 이동시킵니다.
         이 조각(chunk)이 비어 있으면 집합(set)에서 삭제합니다. */
      if ((++mp[min] - buf1) % CHUNK_SIZE == 0)
        mp[min] = mp[--mp_left];
    }
}

static void
verify (void)
{
  size_t buf_idx;
  size_t hist_idx;

  msg ("verify");

  buf_idx = 0;
  for (hist_idx = 0; hist_idx < sizeof histogram / sizeof *histogram;
       hist_idx++)
    {
      while (histogram[hist_idx]-- > 0)
        {
          if (buf2[buf_idx] != hist_idx)
            fail ("bad value %d in byte %zu", buf2[buf_idx], buf_idx);
          buf_idx++;
        }
    }

  msg ("success, buf_idx=%'zu", buf_idx);
}

void
parallel_merge (const char *child_name, int exit_status)
{
  init ();
  sort_chunks (child_name, exit_status);
  merge ();
  verify ();
}
