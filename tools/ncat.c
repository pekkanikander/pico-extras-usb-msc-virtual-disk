// ncat.c
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>

#define BUF_SZ  32768

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file> [<file>...]\n", argv[0]);
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            perror(argv[i]);
            continue;
        }
        if (fcntl(fd, F_NOCACHE, 1) < 0) {
            perror("F_NOCACHE");
            /* fall through: we’ll still try to read */
        }
        char buf[BUF_SZ];
        ssize_t r;
        while ((r = read(fd, buf, BUF_SZ)) > 0) {
            write(STDOUT_FILENO, buf, r);
        }
        close(fd);
    }
    return 0;
}
