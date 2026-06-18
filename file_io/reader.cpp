#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    const std::string file_name = argc > 1 ? argv[1] : "test.txt";
    std::ifstream input(file_name);

    if (!input) {
        std::cerr << "unable to open " << file_name << '\n';
        return 1;
    }

    for (std::string line; std::getline(input, line);) {
        std::cout << line << '\n';
    }

    return 0;
}
