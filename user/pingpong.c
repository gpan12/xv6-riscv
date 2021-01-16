#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int p2c[2];
    pipe(p2c);

    int c2p[2];
    pipe(c2p);

    if (fork() == 0) {
        char buf[1];
        read(p2c[0], buf, 1);
        int pid = getpid();
        printf("%d: received ping\n", pid);
        write(c2p[1], buf, 1);
        exit(0);
    } else {
        write(p2c[1], "a", 1);
        char buf[1];
        wait((int *) 0);
        read(c2p[0], buf, 1);
        int pid = getpid();
        printf("%d: received pong\n", pid);
        exit(0);
    }

}
