// Lab 2 - 24BCS10404 - Rajveer Bishnoi
// read_demo: minimal cat — reads a file and writes it to stdout using raw
// POSIX syscalls. Handles EINTR and partial writes correctly.

#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cerrno>
#include <cstring>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <path>\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        return 1;
    }

    char buf[4096];
    while (true) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n == 0) break;
        if (n == -1) {
            if (errno == EINTR) continue;
            fprintf(stderr, "read failed: %s\n", strerror(errno));
            close(fd);
            return 1;
        }

        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(STDOUT_FILENO, buf + written, n - written);
            if (w == -1) {
                if (errno == EINTR) continue;
                fprintf(stderr, "write failed: %s\n", strerror(errno));
                close(fd);
                return 1;
            }
            written += w;
        }
    }

    close(fd);
    return 0;
}
