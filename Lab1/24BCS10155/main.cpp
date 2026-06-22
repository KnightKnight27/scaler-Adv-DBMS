// Lab Session 1: File I/O in C++ — Kernel Journey via strace
//
// A minimal C++ file reader. Compile, then trace with:
//   g++ -std=c++17 -o reader main.cpp
//   echo "hello from lab 1" > test.txt
//   strace -e trace=openat,read,close,fstat,mmap ./reader
//
// The strace output exposes the user/kernel syscall boundary that an
// std::ifstream open ultimately crosses (openat -> fstat -> read -> close).

#include <iostream>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
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
