#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

char end[16*PGSIZE];

int
main(int argc, char *argv[])
{
  // Your code here.

  char secret[512];

#if 0 // Try to find Physical page that wrote by secret, use cmd: secret xxxxx
  int i;

  for (i = 0; i < 16; i++) {
    if (strcmp("xxxxx", end+16+i*PGSIZE)  == 0) {
        printf("%d\n", i);
        break;
    }
  }
#endif

  strcpy(secret, end+16+9*PGSIZE);
  printf("%s\n", secret);

  exit(0);
}
