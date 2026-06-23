#include <fstream>
#include <iostream>
#include <string>

int main() {
    std::ifstream file("temp.txt");
    if (!file.is_open()) {
        std::cerr << "Failed to open file\n";
        return 1;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::cout << line << '\n';
    }

    return 0;
}
