// Lab 1 - File I/O and the kernel journey
// Akshat Kushwaha | 24BCS10060
//
// A small C++ program that opens a text file, reads it line by line, and
// reports some basic stats (lines, words, bytes). It also asks the kernel
// for the file's inode number and size using stat(), so we can connect the
// C++ side to the filesystem side. If the input file is missing the program
// writes a tiny sample first so it always has something to read.
//
// Build: g++ -std=c++17 -Wall -Wextra file_reader.cpp -o file_reader
// Run:   ./file_reader            (uses notes.txt by default)
//        ./file_reader some.txt   (or pass your own file)

#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>

namespace {

const char* kDefaultFile = "notes.txt";

// Write a couple of sample lines if the file does not exist yet.
void make_sample_if_missing(const std::string& path) {
    std::ifstream probe(path);
    if (probe.good()) return;                 // already there, leave it alone
    std::ofstream out(path);
    out << "Lab 1 reads this file using std::ifstream.\n";
    out << "Under the hood that becomes open() and read() syscalls.\n";
    out << "The kernel resolves the name to an inode and serves bytes.\n";
}

// Count words in a single line (runs of non-space characters).
int words_in(const std::string& line) {
    int count = 0;
    bool inside = false;
    for (char c : line) {
        bool space = (c == ' ' || c == '\t');
        if (!space && !inside) { ++count; inside = true; }
        else if (space)        { inside = false; }
    }
    return count;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string path = (argc > 1) ? argv[1] : kDefaultFile;
    make_sample_if_missing(path);

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "could not open " << path << "\n";
        return 1;
    }

    long lines = 0, words = 0, bytes = 0;
    std::string line;
    std::cout << "----- contents of " << path << " -----\n";
    while (std::getline(file, line)) {
        std::cout << line << "\n";
        ++lines;
        words += words_in(line);
        bytes += static_cast<long>(line.size()) + 1;   // +1 for the newline
    }
    std::cout << "----- end of file -----\n\n";

    std::cout << "lines = " << lines
              << ", words = " << words
              << ", bytes (approx) = " << bytes << "\n";

    // Ask the filesystem about this file directly.
    struct stat info{};
    if (stat(path.c_str(), &info) == 0) {
        std::cout << "inode number  = " << info.st_ino << "\n";
        std::cout << "size on disk  = " << info.st_size << " bytes\n";
        std::cout << "block count   = " << info.st_blocks << " (512B blocks)\n";
    }
    return 0;
}
