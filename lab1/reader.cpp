// Lab 1: a deliberately simple file reader
// Aditya Bhaskara (24BCS10058)
//
// The point of this program is not the C++ itself but the syscalls it triggers.
// Opening the stream becomes an openat()/open(), the first getline pulls bytes
// in with read(), and closing the stream (here, at end of scope) becomes close().
// Tracing the run with strace on Linux, or dtruss on macOS, exposes that journey
// from std::ifstream down through libc, the read() syscall, the VFS, the page
// cache and finally the filesystem.
//
// Build: g++ -std=c++17 -o reader reader.cpp
// Run:   ./reader test.txt

#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const std::string path = (argc > 1) ? argv[1] : "test.txt";

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "could not open " << path << "\n";
        return 1;
    }

    std::string line;
    int line_no = 1;
    while (std::getline(file, line)) {
        std::cout << line_no++ << ": " << line << "\n";
    }
    return 0;
}
