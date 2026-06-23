#include "storage/disk_manager.h"
#include <cstring>
#include <stdexcept>

namespace minidb {

DiskManager::DiskManager(const std::string& db_file) : file_name_(db_file) {
    // Try to open existing file.
    io_.open(db_file, std::ios::binary | std::ios::in | std::ios::out);
    if (!io_.is_open()) {
        // File doesn't exist yet – create it.
        io_.clear();
        io_.open(db_file, std::ios::binary | std::ios::trunc | std::ios::out);
        if (!io_.is_open())
            throw std::runtime_error("DiskManager: cannot create " + db_file);
        io_.close();
        io_.open(db_file, std::ios::binary | std::ios::in | std::ios::out);
        if (!io_.is_open())
            throw std::runtime_error("DiskManager: cannot reopen " + db_file);
    }
    // Determine current page count from file size.
    io_.seekg(0, std::ios::end);
    std::streamoff sz = io_.tellg();
    if (sz < 0) sz = 0;
    next_page_id_ = static_cast<page_id_t>(sz / PAGE_SIZE);
}

DiskManager::~DiskManager() {
    std::lock_guard<std::mutex> lk(latch_);
    if (io_.is_open()) { io_.flush(); io_.close(); }
}

void DiskManager::WritePage(page_id_t page_id, const char* data) {
    std::lock_guard<std::mutex> lk(latch_);
    io_.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
    io_.write(data, PAGE_SIZE);
    if (!io_.good()) { io_.clear(); throw std::runtime_error("DiskManager: write failed"); }
    io_.flush();
}

void DiskManager::ReadPage(page_id_t page_id, char* data) {
    std::lock_guard<std::mutex> lk(latch_);
    io_.seekg(0, std::ios::end);
    std::streamoff sz = io_.tellg();
    std::streamoff off = static_cast<std::streamoff>(page_id) * PAGE_SIZE;
    if (off >= sz) { std::memset(data, 0, PAGE_SIZE); return; }
    io_.seekg(off);
    io_.read(data, PAGE_SIZE);
    if (!io_.good()) { io_.clear(); std::memset(data, 0, PAGE_SIZE); }
}

page_id_t DiskManager::AllocatePage() {
    std::lock_guard<std::mutex> lk(latch_);
    return next_page_id_++;
}

void DiskManager::Flush() {
    std::lock_guard<std::mutex> lk(latch_);
    io_.flush();
}

} // namespace minidb
