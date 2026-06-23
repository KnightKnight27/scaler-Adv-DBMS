#include "minidb/storage/disk_manager.h"

#include <algorithm>
#include <stdexcept>

#include "minidb/common/trace.h"

namespace minidb {

DiskManager::DiskManager(std::filesystem::path path) : path_(std::move(path)) {
  if (path_.has_parent_path()) std::filesystem::create_directories(path_.parent_path());
  if (!std::filesystem::exists(path_)) {
    std::ofstream create(path_, std::ios::binary);
  }
  file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
  if (!file_) throw std::runtime_error("cannot open database file: " + path_.string());
  const auto bytes = std::filesystem::file_size(path_);
  next_page_id_ = static_cast<PageId>((bytes + kPageSize - 1) / kPageSize);
}

DiskManager::~DiskManager() { Flush(); }

PageId DiskManager::AllocatePage() {
  std::scoped_lock lock(mutex_);
  const PageId id = next_page_id_++;
  file_.seekp(static_cast<std::streamoff>(id) * kPageSize);
  std::array<char, kPageSize> zeros{};
  file_.write(zeros.data(), zeros.size());
  file_.flush();
  Trace::Log("DISK", "allocated page " + std::to_string(id));
  return id;
}

void DiskManager::ReadPage(PageId page_id,
                           std::span<std::byte, kPageSize> output) {
  std::scoped_lock lock(mutex_);
  if (page_id < 0 || page_id >= next_page_id_) {
    throw std::out_of_range("invalid page id");
  }
  std::fill(output.begin(), output.end(), std::byte{0});
  file_.clear();
  file_.seekg(static_cast<std::streamoff>(page_id) * kPageSize);
  file_.read(reinterpret_cast<char *>(output.data()), kPageSize);
  file_.clear();
  Trace::Log("DISK", "read page " + std::to_string(page_id));
}

void DiskManager::WritePage(PageId page_id,
                            std::span<const std::byte, kPageSize> input) {
  std::scoped_lock lock(mutex_);
  if (page_id < 0 || page_id >= next_page_id_) {
    throw std::out_of_range("invalid page id");
  }
  file_.clear();
  file_.seekp(static_cast<std::streamoff>(page_id) * kPageSize);
  file_.write(reinterpret_cast<const char *>(input.data()), kPageSize);
  if (!file_) throw std::runtime_error("page write failed");
  Trace::Log("DISK", "wrote page " + std::to_string(page_id));
}

void DiskManager::Flush() {
  std::scoped_lock lock(mutex_);
  if (file_.is_open()) file_.flush();
}

std::size_t DiskManager::PageCount() const {
  std::scoped_lock lock(mutex_);
  return static_cast<std::size_t>(next_page_id_);
}

}  // namespace minidb
