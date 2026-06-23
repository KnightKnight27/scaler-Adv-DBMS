#pragma once
#include <fstream>
#include <string>
#include "storage/page.h"

namespace minidb {

// Owns the single data file and translates page ids to file offsets:
// page N lives at byte N * PAGE_SIZE. This is the only place that touches disk.
class DiskManager {
 public:
  explicit DiskManager(const std::string& db_path);

  void   read_page(PageId id, Page& out);          // load page id into out
  void   write_page(PageId id, const Page& page);  // persist page id
  PageId allocate_page();                          // append a zeroed page, return its id
  PageId num_pages() const { return num_pages_; }

  // Total bytes ever written to disk; used by the benchmark to measure write
  // amplification (how much physical I/O an engine does per logical write).
  uint64_t bytes_written() const { return bytes_written_; }

 private:
  std::fstream file_;
  PageId       num_pages_     = 0;
  uint64_t     bytes_written_ = 0;
};

}  // namespace minidb
