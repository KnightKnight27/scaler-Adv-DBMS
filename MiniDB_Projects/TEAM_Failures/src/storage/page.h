// ============================================================================
// page.h  --  A Page is one in-memory copy of a 4 KB disk block.
//
// The buffer pool owns an array of Page objects ("frames").  Each frame can
// hold the contents of any disk page.  Besides the raw bytes, a frame tracks:
//   * page_id_   : which disk page currently lives here (or INVALID)
//   * pin_count_ : how many callers are currently using this page.  A page with
//                  pin_count > 0 must NOT be evicted -- someone holds a pointer
//                  into its bytes.
//   * is_dirty_  : has this page been modified since it was read from disk?  If
//                  so it must be written back before the frame is reused.
//   * lsn_       : log sequence number of the last WAL record that modified this
//                  page.  Used by recovery to decide whether to redo a change.
// ============================================================================
#pragma once

#include "common/common.h"

namespace minidb {

class Page {
  friend class BufferPool;

 public:
  Page() { reset(); }

  char *data() { return data_; }
  const char *data() const { return data_; }
  page_id_t page_id() const { return page_id_; }
  int pin_count() const { return pin_count_; }
  bool is_dirty() const { return is_dirty_; }
  lsn_t lsn() const { return lsn_; }
  void set_lsn(lsn_t lsn) { lsn_ = lsn; }

 private:
  void reset() {
    memset(data_, 0, PAGE_SIZE);
    page_id_ = INVALID_PAGE_ID;
    pin_count_ = 0;
    is_dirty_ = false;
    lsn_ = INVALID_LSN;
  }

  char      data_[PAGE_SIZE];
  page_id_t page_id_;
  int       pin_count_;
  bool      is_dirty_;
  lsn_t     lsn_;
};

}  // namespace minidb
