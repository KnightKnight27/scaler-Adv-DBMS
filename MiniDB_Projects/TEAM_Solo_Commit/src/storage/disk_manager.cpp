#include "disk_manager.h"

#include <cstring>
#include <sys/stat.h>

namespace minidb {

DiskManager::DiskManager(const std::string& db_file) : file_name_(db_file) {
    // Open for read+write, creating the file if needed (two-step because a fresh
    // file cannot be opened with std::ios::in until it exists).
    io_.open(file_name_, std::ios::in | std::ios::out | std::ios::binary);
    if (!io_.is_open()) {
        io_.clear();
        io_.open(file_name_, std::ios::out | std::ios::binary);
        io_.close();
        io_.open(file_name_, std::ios::in | std::ios::out | std::ios::binary);
    }
    struct stat st;
    if (::stat(file_name_.c_str(), &st) == 0) {
        num_pages_ = static_cast<int>(st.st_size / PAGE_SIZE);
    }
}

DiskManager::~DiskManager() {
    if (io_.is_open()) {
        io_.flush();
        io_.close();
    }
}

void DiskManager::ReadPage(int page_id, char* dest) {
    std::lock_guard<std::mutex> g(latch_);
    io_.clear();
    io_.seekg(static_cast<std::streamoff>(page_id) * PAGE_SIZE, std::ios::beg);
    io_.read(dest, PAGE_SIZE);
    std::streamsize got = io_.gcount();
    if (got < PAGE_SIZE) {
        // Past EOF (e.g. a freshly allocated page): zero-fill the remainder.
        std::memset(dest + got, 0, PAGE_SIZE - got);
        io_.clear();
    }
}

void DiskManager::WritePage(int page_id, const char* src) {
    std::lock_guard<std::mutex> g(latch_);
    io_.clear();
    io_.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE, std::ios::beg);
    io_.write(src, PAGE_SIZE);
    io_.flush();
}

int DiskManager::AllocatePage() {
    std::lock_guard<std::mutex> g(latch_);
    int id = num_pages_++;
    // Materialize the page on disk so the file size reflects the allocation.
    char zero[PAGE_SIZE];
    std::memset(zero, 0, PAGE_SIZE);
    io_.clear();
    io_.seekp(static_cast<std::streamoff>(id) * PAGE_SIZE, std::ios::beg);
    io_.write(zero, PAGE_SIZE);
    io_.flush();
    return id;
}

void DiskManager::Sync() {
    std::lock_guard<std::mutex> g(latch_);
    io_.flush();
}

void DiskManager::Truncate() {
    std::lock_guard<std::mutex> g(latch_);
    if (io_.is_open()) io_.close();
    // Reopen with truncation to wipe the data file, then reopen read/write for normal use.
    io_.open(file_name_, std::ios::out | std::ios::trunc | std::ios::binary);
    io_.close();
    io_.open(file_name_, std::ios::in | std::ios::out | std::ios::binary);
    num_pages_ = 0;
}

}  // namespace minidb
