#pragma once
#include <cstdio>
#include <string>
#include "common/config.h"

namespace minidb {

// DiskManager owns the single database file and performs raw, page-granular
// reads and writes. It knows nothing about page contents -- it just moves
// PAGE_SIZE byte blocks between memory and the file, addressed by PageId.
class DiskManager {
 public:
  explicit DiskManager(const std::string& filename);
  ~DiskManager();

  // Read the page into `data` (must be at least PAGE_SIZE bytes). Reading past
  // the current end of file yields a zero-filled page.
  void ReadPage(PageId page_id, char* data);

  // Write PAGE_SIZE bytes from `data` to the given page and flush.
  void WritePage(PageId page_id, const char* data);

  // Extend the file by one zero-filled page and return its id.
  PageId AllocatePage();

  // Number of pages currently in the file.
  PageId NumPages() const { return num_pages_; }

  // Force buffered file writes to the OS / disk.
  void Sync();

 private:
  std::string filename_;
  FILE*       file_;
  PageId      num_pages_;
};

}  // namespace minidb
