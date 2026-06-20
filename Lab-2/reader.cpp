#include <iostream>
#include <fstream>
#include <string>
#include <vector>

int main() {
    std::cout << "[INFO] Opening file with std::ifstream...\n";
    std::ifstream file("test.txt");
    
    if (!file.is_open()) {
        std::cerr << "[ERROR] Failed to open file\n";
        return 1;
    }
    
    std::cout << "[SUCCESS] File opened\n";
    std::string line;
    int line_count = 0;
    
    std::cout << "[INFO] Reading file line by line...\n";
    while (std::getline(file, line)) {
        std::cout << "[LINE " << ++line_count << "] " << line << "\n";
    }
    
    std::cout << "[INFO] Read " << line_count << " lines\n";
    std::cout << "[INFO] Closing file...\n";
    file.close();
    std::cout << "[SUCCESS] File closed\n";
    
    return 0;
}
