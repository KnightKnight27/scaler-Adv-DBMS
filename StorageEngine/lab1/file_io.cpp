#include <fcntl.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <iostream>

int main() {
    const char* path = "demo.txt";

    // WRITE PHASE

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open for write"); return 1; }

    const char* msg = "hello from kernel side\n";

    ssize_t n = write(fd, msg, std::strlen(msg));
    if (n < 0) { perror("write"); return 1; }
    std::cout << "wrote " << n << " bytes, fd=" << fd << "\n";

    fsync(fd);
    close(fd);

    // READ PHASE 

    fd = open(path, O_RDONLY);
    if (fd < 0) { perror("open for read"); return 1; }

    char buf[128] = {0};

    n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) { perror("read"); return 1; }
    std::cout << "read " << n << " bytes, fd=" << fd << ": " << buf;

    close(fd);
    return 0;
}
