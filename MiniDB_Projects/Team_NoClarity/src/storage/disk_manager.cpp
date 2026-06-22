#include "storage/disk_manager.h"
#include <cstring>
#include <stdexcept>
#include <iostream>

namespace minidb {

DiskManager::DiskManager(const std::string& db_file) : db_file_name_(db_file) {
    db_io_.open(db_file, std::ios::binary | std::ios::in | std::ios::out);
    if (!db_io_.is_open()) {
        // File does not exist, create it
        db_io_.open(db_file, std::ios::binary | std::ios::trunc | std::ios::out | std::ios::in);
        if (!db_io_.is_open()) {
            throw std::runtime_error("Failed to open or create db file: " + db_file);
        }
    }
    // Calculate num_pages_ based on file size
    db_io_.seekg(0, std::ios::end);
    std::streampos size = db_io_.tellg();
    num_pages_ = static_cast<page_id_t>(size / PAGE_SIZE);
}

DiskManager::~DiskManager() {
    std::lock_guard<std::mutex> guard(latch_);
    if (db_io_.is_open()) {
        db_io_.close();
    }
}

void DiskManager::ReadPage(page_id_t page_id, char* page_data) {
    std::lock_guard<std::mutex> guard(latch_);
    std::streamoff offset = static_cast<std::streamoff>(page_id) * PAGE_SIZE;
    
    // Clear status flags before seek/read
    db_io_.clear();
    db_io_.seekg(offset);
    if (!db_io_) {
        std::memset(page_data, 0, PAGE_SIZE);
        return;
    }
    
    db_io_.read(page_data, PAGE_SIZE);
    std::streamsize bytes_read = db_io_.gcount();
    if (bytes_read < static_cast<std::streamsize>(PAGE_SIZE)) {
        std::memset(page_data + bytes_read, 0, PAGE_SIZE - bytes_read);
    }
}

void DiskManager::WritePage(page_id_t page_id, const char* page_data) {
    std::lock_guard<std::mutex> guard(latch_);
    std::streamoff offset = static_cast<std::streamoff>(page_id) * PAGE_SIZE;
    
    db_io_.clear();
    db_io_.seekp(offset);
    db_io_.write(page_data, PAGE_SIZE);
    db_io_.flush();
}

page_id_t DiskManager::AllocatePage() {
    std::lock_guard<std::mutex> guard(latch_);
    page_id_t new_page_id = num_pages_++;
    std::streamoff offset = static_cast<std::streamoff>(new_page_id) * PAGE_SIZE;
    
    db_io_.clear();
    db_io_.seekp(offset);
    char empty_page[PAGE_SIZE] = {0};
    db_io_.write(empty_page, PAGE_SIZE);
    db_io_.flush();
    
    return new_page_id;
}

} // namespace minidb
