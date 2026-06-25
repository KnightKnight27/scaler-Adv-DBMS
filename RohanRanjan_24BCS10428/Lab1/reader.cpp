// Lab 1: A simple C++ file reader.
// Used to trace the syscall journey (openat -> fstat -> read -> close)
// from user-space C++ down to the kernel VFS and inode layer via strace.

#include <iostream>
#include <fstream>
#include <string>

int main() {
    std::ifstream file("test.txt");
    if (!file.is_open()) {
        std::cerr << "Failed to open file\n";
        return 1;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::cout << line << "\n";
    }
    return 0;
}
