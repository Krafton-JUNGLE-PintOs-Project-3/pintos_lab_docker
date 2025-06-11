/* 128kB 파일을 정적 데이터로 읽은 후, 
해당 파일의 바이트들을 카운팅 정렬(counting sort)이라는 단일 패스 알고리즘을 사용하여 "정렬"합니다. 
정렬된 데이터는 같은 파일에 덮어쓰는 방식으로 다시 기록됩니다. */

#include <debug.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"

const char *test_name = "child-sort";

unsigned char buf[128 * 1024];
size_t histogram[256];

int
main (int argc UNUSED, char *argv[]) 
{
  int handle;
  unsigned char *p;
  size_t size;
  size_t i;

  quiet = true;

  CHECK ((handle = open (argv[1])) > 1, "open \"%s\"", argv[1]);

  size = read (handle, buf, sizeof buf);
  for (i = 0; i < size; i++)
    histogram[buf[i]]++;
  p = buf;
  for (i = 0; i < sizeof histogram / sizeof *histogram; i++) 
    {
      size_t j = histogram[i];
      while (j-- > 0)
        *p++ = i;
    }
  seek (handle, 0);
  write (handle, buf, size);
  close (handle);
  
  return 123;
}
