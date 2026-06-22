#include "storage/disk_manager.h"

#include <cstring>
#include <stdexcept>

namespace minidb {

DiskManager::DiskManager(const std::string& filename)
    : filename_(filename), file_(nullptr), num_pages_(0) {
  // Open existing file for read/write; if it does not exist, create it.
  file_ = std::fopen(filename_.c_str(), "rb+");
  if (file_ == nullptr) {
    file_ = std::fopen(filename_.c_str(), "wb+");
    if (file_ == nullptr) {
      throw std::runtime_error("DiskManager: cannot open file " + filename_);
    }
  }

  // Determine how many whole pages the file currently holds.
  std::fseek(file_, 0, SEEK_END);
  long size = std::ftell(file_);
  if (size < 0) size = 0;
  num_pages_ = static_cast<PageId>(size / PAGE_SIZE);
}

DiskManager::~DiskManager() {
  if (file_ != nullptr) {
    std::fflush(file_);
    std::fclose(file_);
    file_ = nullptr;
  }
}

void DiskManager::ReadPage(PageId page_id, char* data) {
  long offset = static_cast<long>(page_id) * PAGE_SIZE;
  std::fseek(file_, offset, SEEK_SET);
  size_t read = std::fread(data, 1, PAGE_SIZE, file_);
  // Pages that have never been written back read as zeros.
  if (read < static_cast<size_t>(PAGE_SIZE)) {
    std::memset(data + read, 0, PAGE_SIZE - read);
  }
}

void DiskManager::WritePage(PageId page_id, const char* data) {
  long offset = static_cast<long>(page_id) * PAGE_SIZE;
  std::fseek(file_, offset, SEEK_SET);
  std::fwrite(data, 1, PAGE_SIZE, file_);
  std::fflush(file_);
  if (page_id >= num_pages_) {
    num_pages_ = page_id + 1;
  }
}

PageId DiskManager::AllocatePage() {
  PageId id = num_pages_;
  char zero[PAGE_SIZE];
  std::memset(zero, 0, PAGE_SIZE);
  WritePage(id, zero);  // bumps num_pages_
  return id;
}

void DiskManager::Sync() {
  if (file_ != nullptr) std::fflush(file_);
}

}  // namespace minidb
