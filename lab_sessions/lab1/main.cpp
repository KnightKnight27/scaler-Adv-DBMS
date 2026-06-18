#include "file_utils.h"
#include <iostream>

int main() {
    std::string target = "test.txt";

    std::cout << "=== VFS Layer Inspection ===\n";
    print_file_inode(target);

    std::cout << "\n=== User Space Buffer Output ===\n";
    print_file_content(target);

    return 0;
}