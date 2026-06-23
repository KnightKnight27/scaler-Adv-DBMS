#include <iostream>
#include <fstream>
#include <string>

int main() {
    std::ifstream inputStream("test.txt");
    
    if (!inputStream.is_open()) {
        std::cerr << "Error: Cannot open test.txt for reading.\n";
        return -1;
    }

    std::cout << "--- Reading File Contents ---\n";
    std::string textContent;
    
    // Read the file line by line with a different structure
    while (std::getline(inputStream, textContent)) {
        std::cout << "-> " << textContent << std::endl;
    }
    std::cout << "-----------------------------\n";

    inputStream.close();
    return 0;
}
