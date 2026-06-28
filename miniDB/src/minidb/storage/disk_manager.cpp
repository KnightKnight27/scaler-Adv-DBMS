#include "minidb/storage/disk_manager.h"

#include <filesystem>

namespace minidb {

DiskManager::DiskManager(std::filesystem::path path) : path_(std::move(path)) {
  if (!std::filesystem::exists(path_)) {
    std::ofstream create(path_, std::ios::binary);
  }
  file_.open(path_, std::ios::binary | std::ios::in | std::ios::out);
  if (!file_) {
    throw MiniDbError("could not open database file: " + path_.string());
  }
}

DiskManager::~DiskManager() { Flush(); }

PageId DiskManager::AllocatePage() {
  std::lock_guard lock(mutex_);
  PageId id = PageCount();
  Page page;
  page.set_page_id(id);
  SlottedPage(page).Init();
  file_.seekp(static_cast<std::streamoff>(id) * static_cast<std::streamoff>(kPageSize));
  file_.write(page.Data(), kPageSize);
  file_.flush();
  return id;
}

void DiskManager::ReadPage(PageId page_id, Page& page) {
  std::lock_guard lock(mutex_);
  page.Reset();
  page.set_page_id(page_id);
  if (page_id >= PageCount()) {
    return;
  }
  file_.seekg(static_cast<std::streamoff>(page_id) * static_cast<std::streamoff>(kPageSize));
  file_.read(page.Data(), kPageSize);
  if (file_.gcount() < static_cast<std::streamsize>(kPageSize)) {
    file_.clear();
  }
}

void DiskManager::WritePage(const Page& page) {
  std::lock_guard lock(mutex_);
  if (page.page_id() == kInvalidPageId) {
    return;
  }
  file_.seekp(static_cast<std::streamoff>(page.page_id()) * static_cast<std::streamoff>(kPageSize));
  file_.write(page.Data(), kPageSize);
  file_.flush();
}

PageId DiskManager::PageCount() const {
  auto size = std::filesystem::exists(path_) ? std::filesystem::file_size(path_) : 0;
  return static_cast<PageId>(size / kPageSize);
}

void DiskManager::Flush() {
  std::lock_guard lock(mutex_);
  if (file_) {
    file_.flush();
  }
}

}  // namespace minidb
