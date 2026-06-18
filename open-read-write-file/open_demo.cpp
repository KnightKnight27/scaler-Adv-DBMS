#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

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

    pid_t pid = getpid();
    printf("opened '%s' as fd=%d in pid=%d\n", argv[1], fd, pid);
    printf("run `lsof -p %d` in another shell to see the open fd\n", pid);

    sleep(30);
    close(fd);
    return 0;
}
