#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char* argv[]) {
    std::ifstream file("test.txt");

    if (!file.is_open()) {
        std::cerr << "Failed to open test.txt\n";
        return 1;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::cout << line << '\n';
    }

    // Use --pause when inspecting the open file through /proc.
    if (argc > 1 && std::string(argv[1]) == "--pause") {
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }

    return 0;
}
