#include <iostream>
#include <fstream>
#include <string>

int main() {
    std::ifstream inputFile("test.txt");

    if (!inputFile) {
        std::cerr << "Failed to open file\n";
        return 1;
    }

    std::string line;
    int lineCount = 0;
    int characterCount = 0;

    while (std::getline(inputFile, line)) {
        std::cout << line << '\n';

        ++lineCount;
        characterCount += line.length();
    }

    std::cout << "\nStatistics\n";
    std::cout << "Lines Read: " << lineCount << '\n';
    std::cout << "Characters Read: " << characterCount << '\n';

    return 0;
}
