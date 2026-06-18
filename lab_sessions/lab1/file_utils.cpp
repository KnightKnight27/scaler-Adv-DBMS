#include "file_utils.h"
#include <iostream>
#include <fstream>
#include <sys/stat.h>

// Simple file reading engine
void print_file_content(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << "\n";
        return;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        std::cout << line << "\n";
    }
}

// Simple metadata extractor using standard POSIX stat
void print_file_inode(const std::string& filename) {
    struct stat file_stat{};
    if (stat(filename.c_str(), &file_stat) == 0) {
        std::cout << "Kernel Inode Pointer: " << file_stat.st_ino << "\n";
    } else {
        std::cerr << "Could not retrieve inode details.\n";
    }
}