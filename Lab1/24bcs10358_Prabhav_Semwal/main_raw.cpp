#include <iostream>
#include <unistd.h>
#include <sys/syscall.h>
#include <fcntl.h>

int main() {
    const char* filename = "raw.txt";

    // raw syscall: openat (modern Linux)
    int fd = syscall(SYS_openat, AT_FDCWD, filename, O_CREAT | O_RDWR, 0644);

    const char* msg = "Hello from RAW syscall!\n";
    syscall(SYS_write, fd, msg, 26);

    syscall(SYS_lseek, fd, 0, SEEK_SET);

    char buffer[100];
    int n = syscall(SYS_read, fd, buffer, 100);

    syscall(SYS_write, 1, buffer, n); // stdout

    syscall(SYS_close, fd);

    return 0;
}
