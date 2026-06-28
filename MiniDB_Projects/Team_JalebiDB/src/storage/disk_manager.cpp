#include "storage/disk_manager.h"
#include <iostream>
#include <stdexcept>
#include <cstring>

namespace minidb {

DiskManager::DiskManager(const std::string &db_file)
    : db_file_name_(db_file), log_file_name_(db_file + ".log") {
    // Open db file
    db_io_.open(db_file_name_, std::ios::binary | std::ios::in | std::ios::out);
    if (!db_io_.is_open()) {
        // Create file if it doesn't exist
        db_io_.clear();
        db_io_.open(db_file_name_, std::ios::binary | std::ios::trunc | std::ios::out | std::ios::in);
        if (!db_io_.is_open()) {
            throw std::runtime_error("Failed to open DB file: " + db_file_name_);
        }
    }

    // Open log file
    log_io_.open(log_file_name_, std::ios::binary | std::ios::in | std::ios::out);
    if (!log_io_.is_open()) {
        log_io_.clear();
        log_io_.open(log_file_name_, std::ios::binary | std::ios::trunc | std::ios::out | std::ios::in);
        if (!log_io_.is_open()) {
            throw std::runtime_error("Failed to open Log file: " + log_file_name_);
        }
    }

    // Determine number of pages
    db_io_.seekp(0, std::ios::end);
    std::streamoff file_size = db_io_.tellp();
    num_pages_ = static_cast<page_id_t>(file_size / PAGE_SIZE);
}

DiskManager::~DiskManager() {
    ShutDown();
}

void DiskManager::WritePage(page_id_t page_id, const char *page_data) {
    std::lock_guard<std::mutex> guard(db_io_lck_);
    db_io_.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
    db_io_.write(page_data, PAGE_SIZE);
    if (db_io_.bad()) {
        std::cerr << "DiskManager: I/O error writing page " << page_id << std::endl;
        return;
    }
    db_io_.flush();
}

void DiskManager::ReadPage(page_id_t page_id, char *page_data) {
    std::lock_guard<std::mutex> guard(db_io_lck_);
    db_io_.seekg(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
    db_io_.read(page_data, PAGE_SIZE);
    
    std::streamsize bytes_read = db_io_.gcount();
    if (bytes_read < PAGE_SIZE) {
        // Fill rest with zeros
        std::memset(page_data + bytes_read, 0, PAGE_SIZE - bytes_read);
    }
}

page_id_t DiskManager::AllocatePage() {
    std::lock_guard<std::mutex> guard(db_io_lck_);
    page_id_t new_page_id = num_pages_++;
    
    // Extend the file by writing a blank page
    char blank_page[PAGE_SIZE] = {0};
    db_io_.seekp(static_cast<std::streamoff>(new_page_id) * PAGE_SIZE);
    db_io_.write(blank_page, PAGE_SIZE);
    db_io_.flush();

    return new_page_id;
}

void DiskManager::WriteLog(const char *log_data, int size) {
    std::lock_guard<std::mutex> guard(db_io_lck_);
    log_io_.seekp(0, std::ios::end);
    log_io_.write(log_data, size);
    log_io_.flush();
}

int DiskManager::ReadLog(char *log_data, int size, int offset) {
    std::lock_guard<std::mutex> guard(db_io_lck_);
    log_io_.seekg(offset);
    log_io_.read(log_data, size);
    return static_cast<int>(log_io_.gcount());
}

int DiskManager::GetLogFileSize() {
    std::lock_guard<std::mutex> guard(db_io_lck_);
    log_io_.seekg(0, std::ios::end);
    return static_cast<int>(log_io_.tellg());
}

void DiskManager::ClearLog() {
    std::lock_guard<std::mutex> guard(db_io_lck_);
    log_io_.close();
    log_io_.open(log_file_name_, std::ios::binary | std::ios::trunc | std::ios::out | std::ios::in);
}

void DiskManager::ShutDown() {
    std::lock_guard<std::mutex> guard(db_io_lck_);
    if (db_io_.is_open()) {
        db_io_.flush();
        db_io_.close();
    }
    if (log_io_.is_open()) {
        log_io_.flush();
        log_io_.close();
    }
}

} // namespace minidb
