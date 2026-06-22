#pragma once
#include <fstream>
#include <string>
#include <mutex>
#include "common/config.h"

namespace minidb {

// Owns the single on-disk database file. Pages are stored contiguously:
// page i lives at byte offset i * PAGE_SIZE. The DiskManager knows nothing
// about page contents -- it just moves PAGE_SIZE-byte blocks to/from disk.
class DiskManager {
 public:
  explicit DiskManager(const std::string& db_file);
  ~DiskManager();

  // Read page `page_id` into `out` (PAGE_SIZE bytes). Zero-fills past EOF.
  void ReadPage(page_id_t page_id, char* out);

  // Write PAGE_SIZE bytes from `data` to page `page_id`, growing the file.
  void WritePage(page_id_t page_id, const char* data);

  // Reserve and return a fresh page id at the end of the file.
  page_id_t AllocatePage();

  page_id_t NumPages() const { return num_pages_; }

  int GetNumWrites() const { return num_writes_; }
  int GetNumReads() const { return num_reads_; }

 private:
  std::string file_name_;
  std::fstream io_;
  page_id_t num_pages_ = 0;
  int num_writes_ = 0;
  int num_reads_ = 0;
  std::mutex latch_;
};

}  // namespace minidb
