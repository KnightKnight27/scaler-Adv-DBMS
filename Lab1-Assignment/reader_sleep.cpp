#include <iostream>
#include <fstream>
#include <string>
#include <unistd.h>

int main() {
    std::ifstream file("test.txt");
    if (!file.is_open()) { std::cerr << "Failed to open file\n"; return 1; }
    std::cout << "PID=" << getpid() << " holding test.txt open for 3s...\n";
    sleep(3);                       // window to inspect /proc
    std::string line;
    while (std::getline(file, line)) std::cout << line << "\n";
    return 0;
}
