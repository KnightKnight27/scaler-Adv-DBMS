#include <iostream>
#include <string>

// CLI entry point. Subcommands are added as milestones land (M1: selftest).
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    std::cout << "MiniDB\n"
              << "usage:\n"
              << "  minidb selftest [dbfile]   run the storage-layer self test\n";
    return 0;
}
