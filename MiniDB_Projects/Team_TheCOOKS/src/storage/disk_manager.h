#pragma once

#include <string>

#include "common/config.h"
#include "common/status.h"

namespace walterdb {

// ---------------------------------------------------------------------------
// DiskManager -- the lowest layer of the storage stack ("Page Manager / Disk
// I/O" in the architecture diagram).  It owns ONE database file and presents it
// as an array of fixed-size pages addressed by page_id (page N lives at byte
// offset N * PAGE_SIZE).  It knows nothing about page contents.
//
// Responsibilities:
//   * allocate_page()  -- grow the file by one page, return its id
//   * read_page/write_page -- pread/pwrite a full page at the right offset
//   * sync() -- fsync, used to honour the write-ahead-log durability rule
//
// Everything above (buffer pool, heap file, B+tree) shares this single global
// page space, which keeps recovery simple: there is one file and one page-id
// space to redo into.
// ---------------------------------------------------------------------------

class DiskManager {
 public:
  // Opens (creating if necessary) the database file at `path`.
  explicit DiskManager(std::string path);
  ~DiskManager();

  DiskManager(const DiskManager&) = delete;
  DiskManager& operator=(const DiskManager&) = delete;

  // Reserve a fresh page at the end of the file and return its id.  The page is
  // zero-filled on disk so a subsequent read is well-defined.
  page_id_t allocate_page();

  // Read page `pid` into `dst` (must be at least PAGE_SIZE bytes).  Reading a
  // page that was allocated but never written yields zeros.
  Status read_page(page_id_t pid, char* dst);

  // Write a full page from `src` (PAGE_SIZE bytes) at page `pid`.
  Status write_page(page_id_t pid, const char* src);

  // Flush OS buffers to stable storage.
  void sync();

  // Number of pages currently allocated in the file.
  page_id_t num_pages() const { return num_pages_; }

  // Cumulative bytes physically written via write_page (for write-amplification
  // measurement in the benchmark).
  uint64_t bytes_written() const { return bytes_written_; }

  const std::string& path() const { return path_; }

 private:
  std::string path_;
  int fd_ = -1;
  page_id_t num_pages_ = 0;
  uint64_t bytes_written_ = 0;
};

}  // namespace walterdb
