#include "storage/disk_manager.h"
#include <cstring>
#include <stdexcept>

namespace minidb {

DiskManager::DiskManager(const std::string &db_file) : file_name_(db_file) {
  // Open for read+write; create the file if it does not yet exist.
  db_io_.open(file_name_, std::ios::in | std::ios::out | std::ios::binary);
  if (!db_io_.is_open()) {
    db_io_.clear();
    db_io_.open(file_name_, std::ios::out | std::ios::binary);  // create
    db_io_.close();
    db_io_.open(file_name_, std::ios::in | std::ios::out | std::ios::binary);
  }
  if (!db_io_.is_open()) {
    throw std::runtime_error("DiskManager: cannot open " + file_name_);
  }
  // Derive how many pages already exist from the file size.
  db_io_.seekg(0, std::ios::end);
  std::streamoff size = db_io_.tellg();
  if (size > 0) next_page_id_ = static_cast<page_id_t>(size / PAGE_SIZE);
}

DiskManager::~DiskManager() {
  if (db_io_.is_open()) {
    db_io_.flush();
    db_io_.close();
  }
}

void DiskManager::ReadPage(page_id_t page_id, char *out) {
  std::streamoff offset = static_cast<std::streamoff>(page_id) * PAGE_SIZE;
  db_io_.clear();
  db_io_.seekg(offset);
  db_io_.read(out, PAGE_SIZE);
  std::streamsize read = db_io_.gcount();
  if (read < PAGE_SIZE) {
    // Reading past the current end of file: zero-fill the remainder.
    std::memset(out + read, 0, PAGE_SIZE - read);
    db_io_.clear();
  }
}

void DiskManager::WritePage(page_id_t page_id, const char *data) {
  std::streamoff offset = static_cast<std::streamoff>(page_id) * PAGE_SIZE;
  db_io_.clear();
  db_io_.seekp(offset);
  db_io_.write(data, PAGE_SIZE);
  db_io_.flush();  // write-through: durability over throughput for clarity
}

page_id_t DiskManager::AllocatePage() { return next_page_id_++; }

}  // namespace minidb
