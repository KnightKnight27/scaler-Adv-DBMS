#pragma once

#include <atomic>
#include <fstream>
#include <mutex>
#include <string>

#include "common/config.h"

namespace minidb {

// The DiskManager is the only component that touches the data file. It maps a
// page id to a byte offset (id * PAGE_SIZE) and reads/writes whole pages. Page
// allocation is a monotonically increasing counter recovered from the file
// size on startup, so ids stay stable across restarts.
class DiskManager {
 public:
  explicit DiskManager(const std::string &db_file);
  ~DiskManager();

  DiskManager(const DiskManager &) = delete;
  DiskManager &operator=(const DiskManager &) = delete;

  void WritePage(page_id_t page_id, const char *page_data);
  void ReadPage(page_id_t page_id, char *page_data);

  page_id_t AllocatePage();                 // hand out the next fresh page id
  void DeallocatePage(page_id_t page_id);   // free-list omitted (stub)

  page_id_t GetNumPages() const { return next_page_id_.load(); }
  int GetNumWrites() const { return num_writes_.load(); }
  int GetNumReads() const { return num_reads_.load(); }

  void ShutDown();

 private:
  std::string file_name_;
  std::fstream db_io_;
  std::mutex io_latch_;
  std::atomic<page_id_t> next_page_id_{0};
  std::atomic<int> num_writes_{0};
  std::atomic<int> num_reads_{0};
};

}  // namespace minidb
