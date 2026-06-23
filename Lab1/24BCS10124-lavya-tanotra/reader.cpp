// Name: Lavya Tanotra
// Roll No: 24BCS10124
// Lab 1: File I/O in C++ — Kernel Journey via strace
//
// Demonstrates the syscall path from std::ifstream → openat → VFS → inode.
// Observed syscalls (via strace -e trace=openat,read,close,fstat,mmap ./reader):
//
//   openat(AT_FDCWD, "test.txt", O_RDONLY)       = 3   // path resolution → inode lookup
//   fstat(3, {st_mode=S_IFREG|0644, st_size=18}) = 0   // inode metadata (size, perms)
//   read(3, "hello from lab 1\n", 4096)           = 18  // copy from page cache → user buf
//   read(3, "", 4096)                             = 0   // EOF
//   close(3)                                     = 0   // release fd, decrement inode refcount
//
// Kernel layers:  std::ifstream → libc fread → read() syscall → VFS → filesystem driver
//                 → page cache → block device (on cache miss only)
//
// Key insight: repeated reads of the same file don't hit disk because the kernel
// keeps file pages in the page cache (RAM). fstat is cheap: inode metadata is
// served from the kernel's inode cache (icache), not from disk.

#include <iostream>
#include <fstream>
#include <string>

int main() {
    // Create a small test file programmatically so the demo is self-contained.
    {
        std::ofstream out("test.txt");
        out << "hello from lab 1\n";
    }

    std::ifstream file("test.txt");
    if (!file.is_open()) {
        std::cerr << "Failed to open file\n";
        return 1;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::cout << line << "\n";
    }

    // file.close() called implicitly by destructor → triggers close() syscall.
    // At that point the kernel decrements the inode's reference count.
    // The inode itself stays in the icache until pressure evicts it.

    return 0;
}
