// Linux file I/O via the POSIX system call interface.
//
// Walks one file through its full lifecycle:
//     open() -> write() -> fsync() -> close() -> open() -> read() -> lseek() -> close()
//
// Each step prints what the kernel is doing so the output reads side-by-side
// with the strace transcript described in README.md.
//
// Build: g++ -std=c++17 -Wall -Wextra -O2 main.cpp -o main
// Run:   ./main
// Trace: strace -e trace=openat,read,write,close,fsync,lseek,fstat ./main

#include <cerrno>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static void die(const char* what) {
    std::cerr << what << " failed: " << std::strerror(errno)
              << " (errno=" << errno << ")\n";
    std::exit(1);
}

int main() {
    const char* filename = "test.txt";
    const char* payload  = "I am writing to this file";

    // ---- open for writing ----
    int fd = ::open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd == -1) die("open(O_WRONLY)");
    std::cout << "open -> fd=" << fd
              << "  (kernel: resolved path, created inode if missing,\n"
              << "                 allocated open file description,\n"
              << "                 installed fd in process FD table,\n"
              << "                 O_TRUNC truncated existing size to 0)\n";

    // ---- write the payload ----
    const std::size_t n = std::strlen(payload);
    ssize_t written = ::write(fd, payload, n);
    if (written == -1) die("write");
    std::cout << "write -> " << written << " bytes\n"
              << "  (kernel: copied user buffer into the page cache as dirty pages,\n"
              << "           updated inode size + mtime, advanced file offset to "
              << written << ".\n"
              << "           Data is NOT on disk yet -- it sits in the page cache.)\n";

    // ---- force durability ----
    if (::fsync(fd) == -1) die("fsync");
    std::cout << "fsync -> 0\n"
              << "  (kernel: flushed this inode's dirty pages from page cache to disk\n"
              << "           and waited for the device ack. Survives a crash now.)\n";

    // ---- close the write fd ----
    if (::close(fd) == -1) die("close");
    std::cout << "close(" << fd << ") -> 0\n";

    // ---- re-open for reading ----
    fd = ::open(filename, O_RDONLY);
    if (fd == -1) die("open(O_RDONLY)");
    std::cout << "open  -> fd=" << fd
              << "  (fresh open file description, offset starts at 0,\n"
              << "                 same inode as before)\n";

    // ---- read it back ----
    char buffer[256] = {};
    ssize_t bytes_read = ::read(fd, buffer, sizeof(buffer) - 1);
    if (bytes_read == -1) die("read");
    std::cout << "read  -> " << bytes_read << " bytes: \"" << buffer << "\"\n"
              << "  (kernel: served from page cache -- no disk I/O,\n"
              << "           advanced offset, bumped atime subject to mount opts)\n";

    // ---- lseek shows the file offset lives in the open file description ----
    off_t pos = ::lseek(fd, 0, SEEK_CUR);
    std::cout << "lseek SEEK_CUR -> " << pos << "  (current offset)\n";
    ::lseek(fd, 0, SEEK_SET);
    bytes_read = ::read(fd, buffer, 5);
    buffer[bytes_read] = '\0';
    std::cout << "after SEEK_SET 0, read 5 bytes: \"" << buffer << "\"\n";

    if (::close(fd) == -1) die("close");
    std::cout << "close(" << fd << ") -> 0\n";
    return 0;
}
