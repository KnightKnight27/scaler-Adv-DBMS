#include <iostream>
#include <fstream>

int main() {
    std::ofstream file("test.txt");

    file << "Hello, this is Lab 1 - File I/O tracing\n";
    file << "Learning inode → VFS → page cache → syscall flow\n";

    file.close();

    std::ifstream input("test.txt");
    std::string line;

    while (std::getline(input, line)) {
        std::cout << line << std::endl;
    }

    input.close();
    return 0;
}