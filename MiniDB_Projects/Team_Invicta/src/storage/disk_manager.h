#pragma once
#include <fstream>
#include <string>
#include "common/config.h"

namespace minidb {

// The DiskManager is the only component that touches the data file. It treats
// the file as an array of fixed-size pages addressed by page_id: page `p` lives
// at byte offset `p * PAGE_SIZE`. It also hands out fresh page ids.
class DiskManager {
 public:
  explicit DiskManager(const std::string &db_file);
  ~DiskManager();

  // Read PAGE_SIZE bytes for `page_id` into `out` (zero-filled past EOF).
  void ReadPage(page_id_t page_id, char *out);

  // Write PAGE_SIZE bytes from `data` to the slot for `page_id`.
  void WritePage(page_id_t page_id, const char *data);

  // Reserve and return the next page id (grows the logical file).
  page_id_t AllocatePage();

  // Number of pages allocated so far.
  page_id_t num_pages() const { return next_page_id_; }

  void Flush() { db_io_.flush(); }

 private:
  std::string  file_name_;
  std::fstream db_io_;
  page_id_t    next_page_id_{0};
};

}  // namespace minidb
