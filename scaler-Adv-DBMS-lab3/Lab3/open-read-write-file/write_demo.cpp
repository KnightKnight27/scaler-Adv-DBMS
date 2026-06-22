// Lab 3 - 24BCS10404 - Rajveer Bishnoi
// write_demo: creates/truncates a file and writes the given text using raw
// POSIX syscalls. Loops to handle partial writes correctly.

#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cerrno>
#include <cstring>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <path> <text>\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        fprintf(stderr, "open failed: %s\n", strerror(errno));
        return 1;
    }

    const char *text = argv[2];
    size_t len = strlen(text);

    size_t written = 0;
    while (written < len) {
        ssize_t w = write(fd, text + written, len - written);
        if (w == -1) {
            if (errno == EINTR) continue;
            fprintf(stderr, "write failed: %s\n", strerror(errno));
            close(fd);
            return 1;
        }
        written += w;
    }

    close(fd);
    printf("wrote %zu bytes to %s\n", len, argv[1]);
    return 0;
}
