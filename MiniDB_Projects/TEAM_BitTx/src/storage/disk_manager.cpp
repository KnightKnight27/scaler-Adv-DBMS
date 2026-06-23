#include "storage/disk_manager.h"

#include "storage/page.h"

#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>

namespace minidb {

using namespace std;

DiskManager::DiskManager(const string& dbFile) : dbFile_(dbFile) {
  file_.open(dbFile_, ios::binary | ios::in | ios::out);
  if (!file_.is_open()) {
    file_.open(dbFile_, ios::binary | ios::in | ios::out | ios::trunc);
    numPages_ = 0;
  } else {
    file_.seekg(0, ios::end);
    int64_t size = file_.tellg();
    numPages_ = static_cast<int32_t>(size / PAGE_SIZE);
    if (size % PAGE_SIZE != 0) {
      throw runtime_error("Corrupt database file: not page-aligned");
    }
  }
}

DiskManager::~DiskManager() {
  if (file_.is_open()) {
    file_.flush();
    file_.close();
  }
}

bool DiskManager::IsOpen() const {
  return file_.is_open();
}

int32_t DiskManager::GetNumPages() const {
  lock_guard<mutex> lock(latch_);
  return numPages_;
}

void DiskManager::ReadPage(int32_t pageId, char* data) {
  lock_guard<mutex> lock(latch_);
  if (pageId < 0 || pageId >= numPages_) {
    throw out_of_range("ReadPage: page id out of range");
  }
  int64_t offset = static_cast<int64_t>(pageId) * PAGE_SIZE;
  file_.seekg(offset, ios::beg);
  file_.read(data, PAGE_SIZE);
  if (file_.gcount() != PAGE_SIZE) {
    throw runtime_error("ReadPage: short read");
  }
}

void DiskManager::WritePage(int32_t pageId, const char* data) {
  lock_guard<mutex> lock(latch_);
  if (pageId < 0) {
    throw out_of_range("WritePage: negative page id");
  }
  int64_t offset = static_cast<int64_t>(pageId) * PAGE_SIZE;
  file_.seekp(offset, ios::beg);
  file_.write(data, PAGE_SIZE);
  file_.flush();
  if (pageId >= numPages_) {
    numPages_ = pageId + 1;
  }
}

int32_t DiskManager::AllocatePage() {
  lock_guard<mutex> lock(latch_);
  if (!freeList_.empty()) {
    int32_t pageId = freeList_.begin()->first;
    freeList_.erase(freeList_.begin());
    return pageId;
  }
  int32_t pid = numPages_++;
  char zero[PAGE_SIZE] = {0};
  int64_t offset = static_cast<int64_t>(pid) * PAGE_SIZE;
  file_.seekp(offset, ios::beg);
  file_.write(zero, PAGE_SIZE);
  file_.flush();
  return pid;
}

void DiskManager::DeallocatePage(int32_t pageId) {
  lock_guard<mutex> lock(latch_);
  if (pageId < 0 || pageId >= numPages_) {
    throw out_of_range("DeallocatePage: page id out of range");
  }
  freeList_[pageId] = pageId;
}

void DiskManager::Sync() {
  lock_guard<mutex> lock(latch_);
  file_.flush();
}

} // namespace minidb
