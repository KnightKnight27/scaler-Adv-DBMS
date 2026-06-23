#pragma once
// Page-granular disk I/O over a single database file. Pages are addressed by a
// dense PageId (0..num_pages). New pages are allocated by appending.
#include "minidb/storage/page.hpp"
#include <cstdint>
#include <fstream>
#include <string>

namespace minidb {

class PageManager {
 public:
  explicit PageManager(const std::string& path);
  ~PageManager();

  PageId allocate_page();                          // appends a zeroed page
  void read_page(PageId id, uint8_t* dst);         // dst must hold kPageSize
  void write_page(PageId id, const uint8_t* src);
  PageId num_pages() const { return num_pages_; }
  void sync();

 private:
  std::string path_;
  std::fstream file_;
  PageId num_pages_ = 0;
};

}  // namespace minidb
