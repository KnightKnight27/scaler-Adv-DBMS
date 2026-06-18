#include "storage/disk_manager.h"

#include <sys/stat.h>

#include <cstring>

#include "common/exception.h"

namespace minidb {

static long FileSizeBytes(const std::string &name) {
  struct stat st {};
  if (stat(name.c_str(), &st) != 0) return -1;
  return static_cast<long>(st.st_size);
}

DiskManager::DiskManager(const std::string &db_file) : file_name_(db_file) {
  // Open existing file for read+write; if it does not exist, create it first.
  db_io_.open(file_name_, std::ios::binary | std::ios::in | std::ios::out);
  if (!db_io_.is_open()) {
    db_io_.clear();
    std::ofstream create(file_name_, std::ios::binary);  // create empty file
    create.close();
    db_io_.open(file_name_, std::ios::binary | std::ios::in | std::ios::out);
    if (!db_io_.is_open()) {
      throw Exception(ErrorKind::kIO, "cannot open or create db file: " + file_name_);
    }
  }
  long sz = FileSizeBytes(file_name_);
  if (sz < 0) sz = 0;
  next_page_id_ = static_cast<page_id_t>(sz / PAGE_SIZE);
}

DiskManager::~DiskManager() { ShutDown(); }

void DiskManager::ShutDown() {
  std::lock_guard<std::mutex> g(io_latch_);
  if (db_io_.is_open()) {
    db_io_.flush();
    db_io_.close();
  }
}

void DiskManager::WritePage(page_id_t page_id, const char *page_data) {
  std::lock_guard<std::mutex> g(io_latch_);
  size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;
  db_io_.seekp(static_cast<std::streamoff>(offset));
  db_io_.write(page_data, PAGE_SIZE);
  if (db_io_.bad()) {
    throw Exception(ErrorKind::kIO, "WritePage failed for page " + std::to_string(page_id));
  }
  db_io_.flush();  // push to OS; durability of WAL is handled separately
  num_writes_++;
}

void DiskManager::ReadPage(page_id_t page_id, char *page_data) {
  std::lock_guard<std::mutex> g(io_latch_);
  size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;
  long sz = FileSizeBytes(file_name_);
  if (sz < 0) sz = 0;
  if (offset >= static_cast<size_t>(sz)) {
    // Reading a freshly-allocated, never-written page: return zeros.
    std::memset(page_data, 0, PAGE_SIZE);
    return;
  }
  db_io_.seekg(static_cast<std::streamoff>(offset));
  db_io_.read(page_data, PAGE_SIZE);
  std::streamsize got = db_io_.gcount();
  if (got < PAGE_SIZE) {  // short read at EOF -> zero-fill the remainder
    db_io_.clear();
    std::memset(page_data + got, 0, PAGE_SIZE - got);
  }
  num_reads_++;
}

page_id_t DiskManager::AllocatePage() { return next_page_id_++; }

void DiskManager::DeallocatePage(page_id_t /*page_id*/) {
  // A real engine threads freed pages onto a free-list page. Omitted here:
  // MiniDB grows the file monotonically, which is sufficient for the lab.
}

}  // namespace minidb
