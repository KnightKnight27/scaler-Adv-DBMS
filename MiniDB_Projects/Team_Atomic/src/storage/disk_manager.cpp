#include "storage/disk_manager.h"
#include "common/types.h"
#include <sys/stat.h>

namespace minidb {

DiskManager::DiskManager(const std::string& db_file) : file_name_(db_file) {
  // Open for read+write in binary. Create the file if it doesn't exist.
  io_.open(file_name_, std::ios::binary | std::ios::in | std::ios::out);
  if (!io_.is_open()) {
    // Create then reopen.
    io_.clear();
    io_.open(file_name_, std::ios::binary | std::ios::trunc | std::ios::out);
    io_.close();
    io_.open(file_name_, std::ios::binary | std::ios::in | std::ios::out);
    if (!io_.is_open()) {
      throw DBError("DiskManager: cannot open db file " + file_name_);
    }
  }
  // Determine how many pages already exist on disk.
  struct stat st {};
  if (::stat(file_name_.c_str(), &st) == 0) {
    num_pages_ = static_cast<page_id_t>(st.st_size / PAGE_SIZE);
  }
}

DiskManager::~DiskManager() {
  if (io_.is_open()) {
    io_.flush();
    io_.close();
  }
}

void DiskManager::ReadPage(page_id_t page_id, char* out) {
  std::lock_guard<std::mutex> lk(latch_);
  num_reads_++;
  size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;
  io_.seekg(static_cast<std::streamoff>(offset));
  io_.read(out, PAGE_SIZE);
  int read = static_cast<int>(io_.gcount());
  if (read < PAGE_SIZE) {
    // Reading past the end of file: zero-fill the remainder.
    std::memset(out + read, 0, PAGE_SIZE - read);
    io_.clear();  // clear EOF/fail bits so the stream stays usable
  }
}

void DiskManager::WritePage(page_id_t page_id, const char* data) {
  std::lock_guard<std::mutex> lk(latch_);
  num_writes_++;
  size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;
  io_.seekp(static_cast<std::streamoff>(offset));
  io_.write(data, PAGE_SIZE);
  io_.flush();  // durability: push to OS so a crash after commit is recoverable
  if (page_id >= num_pages_) num_pages_ = page_id + 1;
}

page_id_t DiskManager::AllocatePage() {
  std::lock_guard<std::mutex> lk(latch_);
  return num_pages_++;
}

}  // namespace minidb
