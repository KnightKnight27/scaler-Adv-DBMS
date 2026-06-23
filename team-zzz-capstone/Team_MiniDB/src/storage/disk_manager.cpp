#include "disk_manager.h"

#include <iostream>

namespace minidb {

DiskManager::DiskManager(const std::string &db_file)
    : db_file_name_(db_file), log_file_name_(db_file + ".wal") {
  db_io_.open(db_file_name_, std::ios::in | std::ios::out | std::ios::binary);
  if (!db_io_.is_open()) {
    // File does not exist yet - create it.
    db_io_.open(db_file_name_, std::ios::out | std::ios::binary);
    db_io_.close();
    db_io_.open(db_file_name_, std::ios::in | std::ios::out | std::ios::binary);
  }
  db_io_.seekg(0, std::ios::end);
  num_pages_ = static_cast<int>(db_io_.tellg() / PAGE_SIZE);

  log_io_.open(log_file_name_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
  if (!log_io_.is_open()) {
    log_io_.open(log_file_name_, std::ios::out | std::ios::binary);
    log_io_.close();
    log_io_.open(log_file_name_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
  }
}

DiskManager::~DiskManager() {
  if (db_io_.is_open()) db_io_.close();
  if (log_io_.is_open()) log_io_.close();
}

void DiskManager::ReadPage(page_id_t page_id, char *page_data) {
  std::lock_guard<std::mutex> guard(db_io_latch_);
  size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;
  db_io_.seekg(static_cast<std::streamoff>(offset));
  db_io_.read(page_data, PAGE_SIZE);
  if (db_io_.eof()) {
    db_io_.clear();
    memset(page_data, 0, PAGE_SIZE);
  }
}

void DiskManager::WritePage(page_id_t page_id, const char *page_data) {
  std::lock_guard<std::mutex> guard(db_io_latch_);
  size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;
  db_io_.seekp(static_cast<std::streamoff>(offset));
  db_io_.write(page_data, PAGE_SIZE);
  db_io_.flush();
}

page_id_t DiskManager::AllocatePage() {
  std::lock_guard<std::mutex> guard(db_io_latch_);
  if (!free_list_.empty()) {
    page_id_t pid = free_list_.back();
    free_list_.pop_back();
    return pid;
  }
  return num_pages_++;
}

void DiskManager::DeallocatePage(page_id_t page_id) {
  std::lock_guard<std::mutex> guard(db_io_latch_);
  free_list_.push_back(page_id);
}

void DiskManager::EnsureCapacity(int min_num_pages) {
  std::lock_guard<std::mutex> guard(db_io_latch_);
  if (min_num_pages > num_pages_) num_pages_ = min_num_pages;
}

void DiskManager::AppendLogRecord(const char *data, size_t size) {
  std::lock_guard<std::mutex> guard(log_io_latch_);
  log_io_.seekp(0, std::ios::end);
  log_io_.write(data, static_cast<std::streamsize>(size));
  log_io_.flush();
}

std::ifstream DiskManager::OpenLogForReading() {
  return std::ifstream(log_file_name_, std::ios::in | std::ios::binary);
}

void DiskManager::FlushLog() {
  std::lock_guard<std::mutex> guard(log_io_latch_);
  log_io_.flush();
}

void DiskManager::TruncateLog() {
  std::lock_guard<std::mutex> guard(log_io_latch_);
  log_io_.close();
  log_io_.open(log_file_name_, std::ios::out | std::ios::binary | std::ios::trunc);
  log_io_.close();
  log_io_.open(log_file_name_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
}

}  // namespace minidb
