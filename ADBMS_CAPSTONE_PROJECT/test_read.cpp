#include <iostream>
#include <fstream>
#include <filesystem>

int main() {
    std::fstream db_io_("minidb_primary.db", std::ios::binary | std::ios::in | std::ios::out);
    if (!db_io_.is_open()) {
        std::cout << "Failed to open file\n";
        return 1;
    }
    char page_data[4096];
    db_io_.seekg(0, std::ios::beg);
    db_io_.read(page_data, 4096);
    int read_count = db_io_.gcount();
    std::cout << "Read count: " << read_count << "\n";
    uint32_t magic = *reinterpret_cast<uint32_t*>(page_data);
    std::cout << "Magic: " << std::hex << magic << std::dec << "\n";
    return 0;
}
