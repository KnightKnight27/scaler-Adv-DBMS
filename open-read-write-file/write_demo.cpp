#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <file> <text>\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "open(%s): %s\n", argv[1], strerror(errno));
        return 1;
    }

    const char *msg = argv[2];
    size_t total = strlen(msg);
    size_t off = 0;

    while (off < total) {
        ssize_t w = write(fd, msg + off, total - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "write: %s\n", strerror(errno));
            close(fd);
            return 1;
        }
        off += w;
    }

    close(fd);
    printf("wrote %zu bytes to %s\n", total, argv[1]);
    return 0;
}
