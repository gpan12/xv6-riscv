#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

int
main(int argc, char *argv[])
{
    char ch;
    char buf[512];
    char *exec_argv[MAXARG];
    int i_arg;
    int j_buf = 0;
    for (i_arg = 0; i_arg < argc-1; i_arg++) {
        exec_argv[i_arg] = argv[i_arg+1];
    }
    while(read(0, &ch, 1) > 0) {
        if (ch == ' ') {
            buf[j_buf] = '\0';
            char tmp[strlen(buf)];
            exec_argv[i_arg] = strcpy(tmp, buf);
            i_arg ++;
            j_buf = 0;
            continue;
        }
        if (ch == '\n') {
            // do fork exec
            buf[j_buf] = '\0';
            char tmp[strlen(buf)];
            exec_argv[i_arg] = strcpy(tmp, buf);
            i_arg ++;
            exec_argv[i_arg] = 0;
            if (fork() == 0) {
                exec(argv[1], exec_argv);
            } else {
                j_buf = 0;
                i_arg = argc - 1;
                wait(0);
                continue;
            }
        }
        buf[j_buf] = ch;
        j_buf ++;
    }
    exit(0);
}
