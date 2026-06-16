#include "syscalls.h"
#include <sys/syscall.h>   // SYS_openat, SYS_read, SYS_write, SYS_close, SYS_lseek, SYS_fsync
#include <fcntl.h>         // AT_FDCWD
#include <unistd.h>        // syscall()

long raw_open(const char *path, int flags, int mode) {
    return syscall(SYS_openat, AT_FDCWD, path, flags, mode);
}

long raw_write(int fd, const void *buf, unsigned long n) {
    return syscall(SYS_write, fd, buf, n);
}

long raw_read(int fd, void *buf, unsigned long n) {
    return syscall(SYS_read, fd, buf, n);
}

long raw_lseek(int fd, long offset, int whence) {
    return syscall(SYS_lseek, fd, offset, whence);
}

long raw_fsync(int fd) {
    return syscall(SYS_fsync, fd);
}

long raw_close(int fd) {
    return syscall(SYS_close, fd);
}
