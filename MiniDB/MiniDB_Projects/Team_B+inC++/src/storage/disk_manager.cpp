#include "disk_manager.hpp"

#include <cstring>
#include <vector>

#include "../common/config.hpp"

DiskManager::DiskManager(const std::string& db_file) : file_name_(db_file) {
    // Open for read+write in binary. If the file does not exist yet, the first
    // open fails, so we create it (out mode) and reopen read+write.
    io_.open(file_name_, std::ios::in | std::ios::out | std::ios::binary);
    if (!io_.is_open()) {
        io_.clear();
        io_.open(file_name_, std::ios::out | std::ios::binary);
        io_.close();
        io_.open(file_name_, std::ios::in | std::ios::out | std::ios::binary);
    }
    // Page count is simply the file size divided by the page size.
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
    // A short read means we ran past end-of-file (e.g. a freshly allocated page
    // that was never written). Zero-fill the remainder and clear the EOF flag.
    std::streamsize got = io_.gcount();
    if (got < static_cast<std::streamsize>(PAGE_SIZE)) {
        std::memset(dest + got, 0, PAGE_SIZE - static_cast<std::size_t>(got));
        io_.clear();
    }
}

void DiskManager::write_page(PageID id, const char* src) {
    io_.seekp(static_cast<std::streamoff>(id) * static_cast<std::streamoff>(PAGE_SIZE), std::ios::beg);
    io_.write(src, PAGE_SIZE);
    io_.flush();  // hand the bytes to the OS so they survive process exit
}

PageID DiskManager::allocate_page() {
    PageID id = num_pages_;
    std::vector<char> zeros(PAGE_SIZE, 0);
    write_page(id, zeros.data());
    num_pages_++;
    return id;
}
