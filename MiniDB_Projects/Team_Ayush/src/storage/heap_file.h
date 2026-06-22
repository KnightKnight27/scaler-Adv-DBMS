#pragma once
#include <functional>
#include <vector>
#include "common/types.h"
#include "storage/buffer_pool.h"

namespace minidb {

// A heap file is an unordered collection of fixed-length records spread across a
// singly-linked chain of pages. Each page has an 8-byte header followed by an
// array of slots; every slot is [status:1 byte][payload:record_size bytes],
// where status 1 = occupied, 0 = free/deleted. Because records are fixed-size,
// deletion just clears the status byte -- no compaction is ever needed.
//
// Page header (bytes):
//   [0..3]  next_page_id (int32, INVALID_PAGE_ID terminates the chain)
//   [4..5]  record_size  (int16)
//   [6..7]  num_used     (int16, occupied slots in this page)
class HeapFile {
 public:
  HeapFile(BufferPool* bp, PageId first_page, int record_size)
      : bp_(bp), first_page_(first_page), record_size_(record_size) {}

  // Create a new (empty) heap file and return the id of its first page.
  static PageId CreateNew(BufferPool* bp, int record_size);

  PageId FirstPage() const { return first_page_; }

  // Insert a record (record_size_ bytes) and return its RID.
  RID Insert(const char* record);

  // Copy the record at `rid` into `out` (must hold record_size_ bytes).
  // Returns false if the slot is empty/deleted.
  bool Get(RID rid, char* out);

  // Overwrite the record at `rid` in place. Returns false if slot empty.
  bool Update(RID rid, const char* record);

  // Mark the record at `rid` as deleted. Returns false if already empty.
  bool Delete(RID rid);

  // Visit every live (RID, record-bytes) pair. The callback receives a pointer
  // valid only during the call.
  void Scan(const std::function<void(RID, const char*)>& visitor);

  // Slots per page for this record size.
  int SlotsPerPage() const;

 private:
  BufferPool* bp_;
  PageId      first_page_;
  int         record_size_;
};

}  // namespace minidb
