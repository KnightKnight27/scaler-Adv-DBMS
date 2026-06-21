#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "buffer/buffer_pool.h"
#include "common/config.h"
#include "common/rid.h"

namespace walterdb {

// ---------------------------------------------------------------------------
// HeapFile -- an unordered collection of variable-length records, stored as a
// singly-linked list of slotted pages and addressed by RID (page_id, slot).
//
// All page access goes through the BufferPool, so this is where "page manager +
// buffer pool integrated" (M1) is actually exercised end-to-end: inserting a
// record fetches/creates a page through the pool, scanning walks the page
// chain through the pool, and everything persists across a reopen because the
// chain head (first_page_id) is the only thing the catalog must remember.
//
// Free-space policy is deliberately simple: inserts append to the last page,
// allocating a new tail page when it is full (a `last_page_id` hint avoids
// re-walking the chain).  Deleted slots are tombstoned and their bytes are not
// reclaimed -- a stated trade-off (reclamation needs in-page compaction, which
// would invalidate outstanding RIDs).  Records larger than a page are rejected
// (no overflow pages).
// ---------------------------------------------------------------------------

class HeapFile {
 public:
  // Open an existing heap whose chain starts at `first_page_id`.
  HeapFile(BufferPool* bpm, page_id_t first_page_id);

  // Create a brand-new (empty) heap; the freshly allocated head page id is
  // available via first_page_id() for the caller/catalog to persist.
  static std::unique_ptr<HeapFile> create(BufferPool* bpm);

  page_id_t first_page_id() const { return first_page_id_; }

  // Largest record this heap can store (a single page's usable capacity).
  static size_t max_record_size();

  RID insert(std::string_view record);
  std::optional<std::string> get(RID rid) const;
  bool erase(RID rid);
  bool update_in_place(RID rid, std::string_view record);

  // Forward cursor over all live records (used by the SeqScan operator and the
  // KV engine's scan()).  Holds at most one page pinned at a time and unpins in
  // its destructor, so it is move-only.
  class Cursor {
   public:
    Cursor() = default;  // end cursor
    Cursor(BufferPool* bpm, page_id_t start_page);
    ~Cursor();
    Cursor(Cursor&& o) noexcept;
    Cursor& operator=(Cursor&& o) noexcept;
    Cursor(const Cursor&) = delete;
    Cursor& operator=(const Cursor&) = delete;

    bool valid() const { return page_ != nullptr; }
    RID rid() const { return rid_; }
    std::string_view value() const { return value_; }
    void next();

   private:
    void release();
    void seek_live();  // position on current-or-next live slot, crossing pages

    BufferPool* bpm_ = nullptr;
    page_id_t page_id_ = INVALID_PAGE_ID;
    class Page* page_ = nullptr;  // pinned while non-null
    slot_id_t slot_ = 0;
    RID rid_{};
    std::string_view value_{};
  };

  Cursor scan() const { return Cursor(bpm_, first_page_id_); }

 private:
  BufferPool* bpm_;
  page_id_t first_page_id_;
  page_id_t last_page_id_;
};

}  // namespace walterdb
