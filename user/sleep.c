#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    write(2, "Missing argument\n", 17);
  }
  sleep(atoi(argv[1]));
  exit(0);
}