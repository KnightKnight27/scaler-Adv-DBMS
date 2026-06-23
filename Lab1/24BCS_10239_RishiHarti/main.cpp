#include <iostream>
#include <fstream>
#include <string>

int main() {
    // 1. Inode / Path Resolution & VFS: std::ifstream open() triggers openat() syscall
    // The kernel walks the directory structure to locate the inode of "test.txt"
    std::ifstream file("test.txt");
    
    if (!file.is_open()) {
        std::cerr << "Error: Could not open test.txt\n";
        return 1;
    }

    std::string line;
    // 2. Page Cache & Buffer Read: std::getline triggers read() syscalls
    // The VFS layer checks the kernel's Page Cache.
    // If the pages are cached, data is read from RAM (Cache Hit).
    // If not, a Block I/O request is sent to the physical storage device (Cache Miss).
    while (std::getline(file, line)) {
        std::cout << line << "\n";
    }

    // 3. Cleanup: Close file stream triggers close() syscall
    // The file descriptor is released and reference count of the active inode is decremented.
    file.close();
    return 0;
}
