#include "storage/disk_manager.h"
#include <iostream>
#include <filesystem>
#include <cstring>

namespace minidb {

DiskManager::DiskManager(const std::string& file_name) : file_name_(file_name) {
    db_io_.open(file_name_, std::ios::binary | std::ios::in | std::ios::out);
    
    // If it doesn't exist, create it
    if (!db_io_.is_open()) {
        db_io_.clear();
        db_io_.open(file_name_, std::ios::binary | std::ios::trunc | std::ios::out);
        db_io_.close();
        db_io_.open(file_name_, std::ios::binary | std::ios::in | std::ios::out);
    }
    
    int file_size = GetFileSize();
    next_page_id_ = file_size / PAGE_SIZE;
}

DiskManager::~DiskManager() {
    if (db_io_.is_open()) {
        db_io_.close();
    }
}

void DiskManager::WritePage(PageId page_id, const char* page_data) {
    std::lock_guard<std::mutex> lock(db_io_mutex_);
    db_io_.seekp(page_id * PAGE_SIZE, std::ios::beg);
    db_io_.write(page_data, PAGE_SIZE);
    if (db_io_.bad()) {
        std::cerr << "I/O error while writing page " << page_id << std::endl;
        return;
    }
    db_io_.flush();
}

void DiskManager::ReadPage(PageId page_id, char* page_data) {
    std::lock_guard<std::mutex> lock(db_io_mutex_);
    int offset = page_id * PAGE_SIZE;
    if (offset >= GetFileSize()) {
        // Read past end of file
        std::cerr << "I/O error reading past end of file: " << page_id << std::endl;
        std::memset(page_data, 0, PAGE_SIZE);
        return;
    }
    db_io_.seekg(offset, std::ios::beg);
    db_io_.read(page_data, PAGE_SIZE);
    
    // If less than PAGE_SIZE was read, fill rest with 0s
    std::streamsize read_count = db_io_.gcount();
    if (read_count < static_cast<std::streamsize>(PAGE_SIZE)) {
        db_io_.clear(); // Clear EOF flags
        std::memset(page_data + read_count, 0, PAGE_SIZE - read_count);
    }
}

PageId DiskManager::AllocatePage() {
    std::lock_guard<std::mutex> lock(db_io_mutex_);
    PageId new_page_id = next_page_id_++;
    
    // Extend the file by writing a zeroed page
    db_io_.seekp(new_page_id * PAGE_SIZE, std::ios::beg);
    char empty_page[PAGE_SIZE] = {0};
    db_io_.write(empty_page, PAGE_SIZE);
    db_io_.flush();
    
    return new_page_id;
}

int DiskManager::GetFileSize() {
    // Keep seek state simple by doing it under lock just in case it's used concurrently,
    // though usually not strictly needed for tellg if done right.
    // std::filesystem::file_size is simpler
    try {
        return std::filesystem::file_size(file_name_);
    } catch (...) {
        return 0;
    }
}

} // namespace minidb
