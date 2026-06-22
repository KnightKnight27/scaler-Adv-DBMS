#include "storage/disk_storage_manager.h"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace axiomdb {

DiskStorageManager::DiskStorageManager(std::string path) : path_(std::move(path)) {
  fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT, 0644);
  if (fd_ < 0) {
    throw std::runtime_error("DiskStorageManager: cannot open '" + path_ + "': " + std::strerror(errno));
  }
  off_t size = ::lseek(fd_, 0, SEEK_END);
  if (size < 0) {
    throw std::runtime_error("DiskStorageManager: lseek failed: " + std::string(std::strerror(errno)));
  }
  // A well-formed file is a whole number of pages.  A trailing partial page can
  // only come from a crash mid-write; we round down and let recovery sort it.
  num_pages_ = static_cast<page_id_t>(size / PAGE_SIZE);
}

DiskStorageManager::~DiskStorageManager() {
  if (fd_ >= 0) ::close(fd_);
}

page_id_t DiskStorageManager::allocate_page() {
  page_id_t pid = num_pages_++;
  // Materialise the page on disk (zero-filled) so the file actually grows and a
  // later read of this page is defined even before anything writes content.
  std::array<char, PAGE_SIZE> zeros{};
  write_page(pid, zeros.data());
  return pid;
}

Status DiskStorageManager::read_page(page_id_t pid, char* dst) {
  if (pid < 0) return Status::invalid_argument("read_page: negative page id");
  off_t offset = static_cast<off_t>(pid) * PAGE_SIZE;
  ssize_t n = ::pread(fd_, dst, PAGE_SIZE, offset);
  if (n < 0) return Status::io_error(std::string("pread: ") + std::strerror(errno));
  if (static_cast<size_t>(n) < PAGE_SIZE) {
    // Page allocated but file not yet extended that far (or short read): the
    // remainder is logically zero.
    std::memset(dst + n, 0, PAGE_SIZE - static_cast<size_t>(n));
  }
  return {};
}

Status DiskStorageManager::write_page(page_id_t pid, const char* src) {
  if (pid < 0) return Status::invalid_argument("write_page: negative page id");
  off_t offset = static_cast<off_t>(pid) * PAGE_SIZE;
  ssize_t n = ::pwrite(fd_, src, PAGE_SIZE, offset);
  if (n < 0) return Status::io_error(std::string("pwrite: ") + std::strerror(errno));
  if (static_cast<size_t>(n) != PAGE_SIZE) return Status::io_error("pwrite: short write");
  bytes_written_ += PAGE_SIZE;
  return {};
}

void DiskStorageManager::sync() {
  if (fd_ >= 0) ::fsync(fd_);
}

}  // namespace axiomdb
