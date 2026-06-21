#include "disk_manager.hpp"

#include <cstring>
#include <vector>

#include "../common/config.hpp"

DiskManager::DiskManager(const std::string& db_file) : file_name_(db_file) {
    // doesn't exist yet -> create then reopen rw
    io_.open(file_name_, std::ios::in | std::ios::out | std::ios::binary);
    if (!io_.is_open()) {
        io_.clear();
        io_.open(file_name_, std::ios::out | std::ios::binary);
        io_.close();
        io_.open(file_name_, std::ios::in | std::ios::out | std::ios::binary);
    }
    // page count = file size / page size
    io_.seekg(0, std::ios::end);
    std::streampos end = io_.tellg();
    num_pages_ = (end <= 0) ? 0 : static_cast<PageID>(end / static_cast<std::streamoff>(PAGE_SIZE));
}

DiskManager::~DiskManager() {
    if (io_.is_open()) {
        io_.flush();
        io_.close();
    }
}

void DiskManager::read_page(PageID id, char* dest) {
    io_.seekg(static_cast<std::streamoff>(id) * static_cast<std::streamoff>(PAGE_SIZE), std::ios::beg);
    io_.read(dest, PAGE_SIZE);
    // short read = past EOF, zero-fill rest
    std::streamsize got = io_.gcount();
    if (got < static_cast<std::streamsize>(PAGE_SIZE)) {
        std::memset(dest + got, 0, PAGE_SIZE - static_cast<std::size_t>(got));
        io_.clear();
    }
}

void DiskManager::write_page(PageID id, const char* src) {
    io_.seekp(static_cast<std::streamoff>(id) * static_cast<std::streamoff>(PAGE_SIZE), std::ios::beg);
    io_.write(src, PAGE_SIZE);
    io_.flush();
}

PageID DiskManager::allocate_page() {
    PageID id = num_pages_;
    std::vector<char> zeros(PAGE_SIZE, 0);
    write_page(id, zeros.data());
    num_pages_++;
    return id;
}
