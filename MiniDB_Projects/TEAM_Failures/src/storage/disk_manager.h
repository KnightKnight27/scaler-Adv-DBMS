// ============================================================================
// disk_manager.h  --  The only component that touches the data file directly.
//
// The DiskManager turns "page id" into "byte offset" and performs the actual
// read()/write() at offset = page_id * PAGE_SIZE.  Everything above it (buffer
// pool, heap files, B+ tree) thinks purely in terms of page ids and never knows
// where a page physically sits.  This is the bottom of the storage stack.
//
// Allocation policy: append-only.  allocatePage() hands out the next page id and
// the file grows when that page is first written.  (A production system would
// also maintain a free list to reuse pages from dropped tables; we document that
// as a limitation rather than implement it, to keep the code readable.)
// ============================================================================
#pragma once

#include "common/common.h"

namespace minidb {

class DiskManager {
 public:
  explicit DiskManager(const string &db_file);
  ~DiskManager();

  // Read the page with the given id into `dest` (must be >= PAGE_SIZE bytes).
  void readPage(page_id_t page_id, char *dest);

  // Write PAGE_SIZE bytes from `src` to the given page id, flushing to the OS.
  void writePage(page_id_t page_id, const char *src);

  // Reserve and return a fresh page id.  Does not write anything yet.
  page_id_t allocatePage();

  // Number of pages the file can currently address.
  int numPages() const { return num_pages_; }

  void shutdown();

 private:
  string   file_name_;
  fstream  io_;          // the open data file
  int           num_pages_;   // == file size / PAGE_SIZE
  mutex    latch_;       // serialize file access (the file is shared state)
};

}  // namespace minidb
