#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// Function to implement strcat() function in C
char* strcat(char* destination, const char* source)
{
    // make ptr point to the end of destination string
    char* ptr = destination + strlen(destination);
 
    // Appends characters of source to the destination string
    while (*source != '\0')
        *ptr++ = *source++;
 
    // null terminate destination string
    *ptr = '\0';
 
    // destination is returned by standard strcat()
    return destination;
}

int
open_path(char* path, struct stat *st, int *fd)
{
    if((*fd = open(path, 0)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return 1;
    }

    if(fstat(*fd, st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(*fd);
        return 1;
    }
    return 0;
}

void
find(char *path, char *search_string)
{
    int fd;
    struct dirent de;
    struct stat st;

    if (open_path(path, &st, &fd)) {
        return;
    }

    if (st.type != T_DIR) {
        close(fd);
        return;
    }

    while(read(fd, &de, sizeof(de)) == sizeof(de)){
        if(de.inum == 0)
            continue;
        char buf[512];
        strcpy(buf, path);
        strcat(buf, "/");
        strcat(buf, de.name);

        if (( strcmp(de.name, search_string)) == 0) {
            printf("%s\n", buf);
        }
        if (strcmp(de.name, ".") == 0) {
            continue;
        }
        if (strcmp(de.name, "..") == 0) {
            continue;
        }
        find(buf, search_string);
    }
    close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc < 3){
    printf("Format is find [folder] [string]\n");
    exit(0);
  }
  find(argv[1], argv[2]);
  exit(0);
}