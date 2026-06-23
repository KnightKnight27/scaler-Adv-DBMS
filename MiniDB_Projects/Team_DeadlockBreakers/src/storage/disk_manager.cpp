#include "storage/disk_manager.h"
#include <cassert>
#include <cstring>
#include <filesystem>

namespace minidb {

DiskManager::DiskManager(const std::string& db_file) : file_name_(db_file) {
    // Open existing file or create new one
    file_.open(db_file, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_.is_open()) {
        // File doesn't exist, create it
        file_.open(db_file, std::ios::out | std::ios::binary);
        file_.close();
        file_.open(db_file, std::ios::in | std::ios::out | std::ios::binary);
    }

    // Determine number of existing pages
    file_.seekg(0, std::ios::end);
    auto file_size = file_.tellg();
    num_pages_ = static_cast<page_id_t>(file_size / PAGE_SIZE);
    file_.seekg(0);
}

DiskManager::~DiskManager() {
    if (file_.is_open()) {
        file_.close();
    }
}

void DiskManager::ReadPage(page_id_t page_id, char* data) {
    std::lock_guard<std::mutex> lock(io_mutex_);
    assert(page_id < num_pages_);
    auto offset = static_cast<std::streamoff>(page_id) * PAGE_SIZE;
    file_.seekg(offset);
    file_.read(data, PAGE_SIZE);
}

void DiskManager::WritePage(page_id_t page_id, const char* data) {
    std::lock_guard<std::mutex> lock(io_mutex_);
    auto offset = static_cast<std::streamoff>(page_id) * PAGE_SIZE;
    file_.seekp(offset);
    file_.write(data, PAGE_SIZE);
    file_.flush();
}

page_id_t DiskManager::AllocatePage() {
    std::lock_guard<std::mutex> lock(io_mutex_);
    page_id_t new_pid = num_pages_++;
    // Extend file by writing an empty page
    char empty[PAGE_SIZE];
    std::memset(empty, 0, PAGE_SIZE);
    auto offset = static_cast<std::streamoff>(new_pid) * PAGE_SIZE;
    file_.seekp(offset);
    file_.write(empty, PAGE_SIZE);
    file_.flush();
    return new_pid;
}

} // namespace minidb
