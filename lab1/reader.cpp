// Lab 1 — File I/O in C++: Kernel Journey via strace
// Simple C++ file reader used with strace to observe the underlying
// syscalls (openat, fstat, read, mmap, close) issued by std::ifstream.

#include <iostream>
#include <fstream>
#include <string>

int main(int argc, char* argv[]) {
    // Default to "test.txt" if no path is provided
    const std::string path = (argc > 1) ? argv[1] : "test.txt";

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << path << "\n";
        return 1;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::cout << line << "\n";
    }

    return 0;
}
