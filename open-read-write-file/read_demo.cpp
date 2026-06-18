#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#define BUF_SIZE 4096

static int write_all(int fd, const char *buf, ssize_t n) {
    ssize_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += w;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file>\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "open(%s): %s\n", argv[1], strerror(errno));
        return 1;
    }

    char buf[BUF_SIZE];
    for (;;) {
        ssize_t n = read(fd, buf, BUF_SIZE);
        if (n == 0) break;
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "read: %s\n", strerror(errno));
            close(fd);
            return 1;
        }
        if (write_all(STDOUT_FILENO, buf, n) < 0) {
            fprintf(stderr, "write: %s\n", strerror(errno));
            close(fd);
            return 1;
        }
    }

    close(fd);
    return 0;
}
