#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: hex_dump <file>\n";
        return 1;
    }

    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "Failed to open file\n";
        return 2;
    }

    constexpr std::size_t width = 16;
    unsigned char buffer[width];
    std::size_t offset = 0;

    while (input) {
        input.read(reinterpret_cast<char *>(buffer), static_cast<std::streamsize>(width));
        const std::streamsize count = input.gcount();
        if (count <= 0) {
            break;
        }

        std::cout << std::hex << std::setw(8) << std::setfill('0') << offset << "  ";

        for (std::size_t i = 0; i < width; ++i) {
            if (i < static_cast<std::size_t>(count)) {
                std::cout << std::setw(2) << static_cast<int>(buffer[i]) << ' ';
            } else {
                std::cout << "   ";
            }
            if (i == 7) {
                std::cout << ' ';
            }
        }

        std::cout << " |";
        for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
            const unsigned char ch = buffer[i];
            std::cout << (std::isprint(ch) ? static_cast<char>(ch) : '.');
        }
        std::cout << "|\n";

        offset += static_cast<std::size_t>(count);
    }

    return 0;
}