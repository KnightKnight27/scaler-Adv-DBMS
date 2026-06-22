#include "disk_manager.hpp"
#include <vector>
#include <algorithm>
#include <stdexcept>

namespace minidb {

DiskManager::DiskManager(const std::string& path) : path_(path) {
    // Open for read+write. Create the file first if it does not exist yet.
    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_.is_open()) {
        std::ofstream create(path_, std::ios::out | std::ios::binary);
        create.close();
        file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
    }
    if (!file_.is_open()) throw std::runtime_error("cannot open data file: " + path_);

    file_.seekg(0, std::ios::end);
    std::streamoff bytes = file_.tellg();
    num_pages_ = static_cast<PageId>(bytes / static_cast<std::streamoff>(PAGE_SIZE));
}

DiskManager::~DiskManager() {
    if (file_.is_open()) file_.close();
}

void DiskManager::readPage(PageId id, char* dst) {
    if (id < 0 || id >= num_pages_) throw std::runtime_error("readPage: bad page id");
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(id) * PAGE_SIZE, std::ios::beg);
    file_.read(dst, PAGE_SIZE);
    // A short read (file not fully written) is zero-padded so callers see a
    // consistent PAGE_SIZE buffer.
    std::streamsize got = file_.gcount();
    if (got < static_cast<std::streamsize>(PAGE_SIZE))
        std::fill(dst + got, dst + PAGE_SIZE, 0);
    file_.clear();
}

void DiskManager::writePage(PageId id, const char* src) {
    if (id < 0 || id >= num_pages_) throw std::runtime_error("writePage: bad page id");
    file_.clear();
    file_.seekp(static_cast<std::streamoff>(id) * PAGE_SIZE, std::ios::beg);
    file_.write(src, PAGE_SIZE);
    file_.flush();
}

PageId DiskManager::allocatePage() {
    PageId id = num_pages_;
    std::vector<char> zero(PAGE_SIZE, 0);
    file_.clear();
    file_.seekp(static_cast<std::streamoff>(id) * PAGE_SIZE, std::ios::beg);
    file_.write(zero.data(), PAGE_SIZE);
    file_.flush();
    num_pages_++;
    return id;
}

} // namespace minidb
