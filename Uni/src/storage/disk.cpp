#include "storage/disk.h"
#include <iostream>
#include <stdexcept>
#include <cstring>

DiskManager::DiskManager(const std::string& db_file) : db_file_name_(db_file) {
    // Attempt to open existing
    db_io_.open(db_file_name_, std::ios::binary | std::ios::in | std::ios::out);
    if (!db_io_.is_open()) {
        // Create new
        db_io_.open(db_file_name_, std::ios::binary | std::ios::trunc | std::ios::out | std::ios::in);
        if (!db_io_.is_open()) {
            throw std::runtime_error("Failed to open or create database file: " + db_file_name_);
        }
    }
    // Calculate number of pages based on file size
    db_io_.seekp(0, std::ios::end);
    std::streamoff file_size = db_io_.tellp();
    num_pages_ = static_cast<int>(file_size / PAGE_SIZE);
}

DiskManager::~DiskManager() {
    ShutDown();
}

void DiskManager::ReadPage(PageId_t page_id, char* page_data) {
    std::lock_guard<std::mutex> guard(db_io_mutex_);
    int offset = page_id * PAGE_SIZE;
    db_io_.seekg(offset);
    db_io_.read(page_data, PAGE_SIZE);
    std::streamsize bytes_read = db_io_.gcount();
    if (bytes_read < PAGE_SIZE) {
        std::memset(page_data + bytes_read, 0, PAGE_SIZE - bytes_read);
    }
}

void DiskManager::WritePage(PageId_t page_id, const char* page_data) {
    std::lock_guard<std::mutex> guard(db_io_mutex_);
    int offset = page_id * PAGE_SIZE;
    db_io_.seekp(offset);
    db_io_.write(page_data, PAGE_SIZE);
    db_io_.flush();
}

PageId_t DiskManager::AllocatePage() {
    std::lock_guard<std::mutex> guard(db_io_mutex_);
    PageId_t new_page_id = num_pages_++;
    int offset = new_page_id * PAGE_SIZE;
    db_io_.seekp(offset);
    char zero[PAGE_SIZE] = {0};
    db_io_.write(zero, PAGE_SIZE);
    db_io_.flush();
    return new_page_id;
}

void DiskManager::DeallocatePage(PageId_t page_id) {
    // In MiniDB, deallocated pages can be left as holes, or handled in buffer.
    // For simplicity, we don't truncate the file immediately.
    (void)page_id;
}

void DiskManager::ShutDown() {
    std::lock_guard<std::mutex> guard(db_io_mutex_);
    if (db_io_.is_open()) {
        db_io_.close();
    }
}

void DiskManager::Clear() {
    std::lock_guard<std::mutex> guard(db_io_mutex_);
    if (db_io_.is_open()) {
        db_io_.close();
    }
    db_io_.open(db_file_name_, std::ios::binary | std::ios::trunc | std::ios::out | std::ios::in);
    num_pages_ = 0;
}
