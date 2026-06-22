#include "storage.h"

#include <cstring>
#include <stdexcept>

DiskManager::DiskManager(const std::string &db_file) : file_name_(db_file) {
    db_io_.open(db_file, std::ios::binary | std::ios::in | std::ios::out);
    if (!db_io_.is_open()) {
        // File doesn't exist yet — create it.
        db_io_.open(db_file, std::ios::binary | std::ios::trunc | std::ios::in | std::ios::out);
        if (!db_io_.is_open()) {
            throw std::runtime_error("Cannot open database file: " + db_file);
        }
        num_pages_ = 0;
    } else {
        db_io_.seekg(0, std::ios::end);
        int64_t file_size = db_io_.tellg();
        num_pages_ = static_cast<int32_t>(file_size / PAGE_SIZE);
    }
}

DiskManager::~DiskManager() {
    if (db_io_.is_open()) {
        db_io_.flush();
        db_io_.close();
    }
}

page_id_t DiskManager::AllocatePage() {
    return num_pages_++;
}

void DiskManager::WritePage(page_id_t page_id, const char *page_data) {
    if (page_id < 0) {
        throw std::invalid_argument("Invalid page_id in WritePage");
    }
    db_io_.seekp(PageOffset(page_id), std::ios::beg);
    db_io_.write(page_data, PAGE_SIZE);
    if (db_io_.bad()) {
        throw std::runtime_error("WritePage: I/O error");
    }
    db_io_.flush();
}

void DiskManager::ReadPage(page_id_t page_id, char *page_data) {
    if (page_id < 0 || page_id >= num_pages_) {
        throw std::out_of_range("ReadPage: page_id out of range");
    }
    db_io_.seekg(PageOffset(page_id), std::ios::beg);
    db_io_.read(page_data, PAGE_SIZE);
    if (db_io_.bad()) {
        throw std::runtime_error("ReadPage: I/O error");
    }
    // If fewer bytes were read (e.g. sparse file), zero-fill the remainder.
    int64_t bytes_read = db_io_.gcount();
    if (bytes_read < static_cast<int64_t>(PAGE_SIZE)) {
        std::memset(page_data + bytes_read, 0, PAGE_SIZE - bytes_read);
    }
    db_io_.clear(); // clear any eof/fail bits
}
