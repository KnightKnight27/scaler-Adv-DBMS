#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {

constexpr const char *kFileName = "lab1_syscall_output.txt";
constexpr const char *kMessage =
    "Lab 1 raw system call demo\n"
    "This file was opened, written, repositioned, read, and closed using syscall().\n";

#if defined(__APPLE__)
constexpr long kFstatSyscall = SYS_fstat64;
#else
constexpr long kFstatSyscall = SYS_fstat;
#endif

void print_error_and_exit(const char *operation) {
    std::fprintf(stderr, "%s failed: %s\n", operation, std::strerror(errno));
    _exit(1);
}

long raw_open_for_rw(const char *path) {
    long fd = syscall(SYS_openat, AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) {
        print_error_and_exit("openat");
    }
    return fd;
}

void raw_write_all(long fd, const char *buffer, std::size_t size) {
    std::size_t written = 0;

    while (written < size) {
        long result = syscall(SYS_write, fd, buffer + written, size - written);
        if (result < 0) {
            print_error_and_exit("write");
        }
        written += static_cast<std::size_t>(result);
    }
}

void raw_seek_start(long fd) {
    long result = syscall(SYS_lseek, fd, 0, SEEK_SET);
    if (result < 0) {
        print_error_and_exit("lseek");
    }
}

long raw_read(long fd, char *buffer, std::size_t size) {
    long bytes_read = syscall(SYS_read, fd, buffer, size);
    if (bytes_read < 0) {
        print_error_and_exit("read");
    }
    return bytes_read;
}

void raw_print_stat(long fd) {
    struct stat file_stat {};
    long result = syscall(kFstatSyscall, fd, &file_stat);
    if (result < 0) {
        print_error_and_exit("fstat");
    }

    std::printf("File descriptor: %ld\n", fd);
    std::printf("Inode number   : %llu\n", static_cast<unsigned long long>(file_stat.st_ino));
    std::printf("File size      : %lld bytes\n", static_cast<long long>(file_stat.st_size));
}

void raw_close(long fd) {
    long result = syscall(SYS_close, fd);
    if (result < 0) {
        print_error_and_exit("close");
    }
}

}  // namespace

int main() {
    long fd = raw_open_for_rw(kFileName);

    raw_write_all(fd, kMessage, std::strlen(kMessage));
    raw_seek_start(fd);

    char read_buffer[512] {};
    long bytes_read = raw_read(fd, read_buffer, sizeof(read_buffer) - 1);
    read_buffer[bytes_read] = '\0';

    raw_print_stat(fd);
    std::printf("\nData read from file:\n%s", read_buffer);

    raw_close(fd);
    return 0;
}

