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

    printf("opened %s -> fd %d (pid %d)\n", argv[1], fd, getpid());
    printf("sleeping 30s — inspect with: lsof -p %d\n", getpid());
    sleep(30);

    close(fd);
    return 0;
}
