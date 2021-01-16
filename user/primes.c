#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void create_worker(int starting_num, int *p);

int
main(int argc, char *argv[])
{
    int p[2];
    pipe(p);

    // int tb = 57;
    // write(p[1], &tb, sizeof(tb));
    // int v;
    // read(p[0], &v, sizeof(v));
    // printf("%d\n", v);
    // exit(0);

    printf("prime: 2\n");
    create_worker(3, p);
    int buf;
    for (int i = 5; i < 39; i++) {
        if ((i % 2) == 0) {
            continue;
        }
        buf = i;
        write(p[1], &buf, sizeof(buf));
    }
    buf = -1;
    write(p[1], &buf, sizeof(buf));
    close(p[1]);
    wait(0);
    exit(0);
}

void
create_worker(int starting_num, int* pipe2read)
{
    int rp = pipe2read[0];
    if (fork() == 0) {
        close(pipe2read[1]);
        int created_worker = 0;
        printf("prime %d\n", starting_num);
        int p[2];
        pipe(p);
        int buf;
        while (1) {
            read(rp, &buf, sizeof(buf));
            // printf("%d\n", buf);
            if (buf == -1) {
                write(p[1], &buf, sizeof(buf));
                close(p[1]);
                wait(0);
                exit(0);
            }
            if ((buf % starting_num) == 0) {
                continue;
            } else {
                if (created_worker) {
                    write(p[1], &buf, sizeof(buf));
                } else {
                    create_worker(buf, p);
                    created_worker = 1;
                }
            }
        }
    } else {
        close(pipe2read[0]);
    }
}