#pragma once
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "page.h"

namespace minidb {

// DiskManager owns a single heap file on disk and is responsible for the
// page-based abstraction: allocating new pages, and reading/writing whole
// PAGE_SIZE blocks at a given offset. It has no notion of caching - that is
// the BufferPool's job. It also exposes raw append-only log I/O used by the
// WAL manager (recovery module), kept here because both are "disk access".
class DiskManager {
 public:
  explicit DiskManager(const std::string &db_file);
  ~DiskManager();

  void ReadPage(page_id_t page_id, char *page_data);
  void WritePage(page_id_t page_id, const char *page_data);

  // Allocates a new page at the end of the file (or reuses a freed page id)
  // and returns its id. Does not initialize content - caller must do that.
  page_id_t AllocatePage();
  void DeallocatePage(page_id_t page_id);
  // Bumps the page-id counter so the next AllocatePage() can't collide
  // with a page_id that's already referenced (e.g. by the WAL) but never
  // reached disk before a crash, since on-disk file size alone would
  // otherwise look like that page was never allocated.
  void EnsureCapacity(int min_num_pages);

  int GetNumPages() const { return num_pages_; }

  // --- WAL log file helpers ---
  void AppendLogRecord(const char *data, size_t size);
  std::ifstream OpenLogForReading();
  void FlushLog();
  // Clears the WAL - called after recovery has applied everything and
  // flushed data pages, acting as a checkpoint so future restarts don't
  // re-replay history that's now durable in the data file.
  void TruncateLog();

 private:
  std::fstream db_io_;
  std::fstream log_io_;
  std::string db_file_name_;
  std::string log_file_name_;
  int num_pages_ = 0;
  std::vector<page_id_t> free_list_;
  std::mutex db_io_latch_;
  std::mutex log_io_latch_;
};

}  // namespace minidb
