#pragma once
#include <vector>
#include "common/types.h"
#include "storage/buffer_pool.h"

namespace minidb {

// A table's rows stored as a chain of pages linked by PageHeader::next_page.
// New rows are appended to the last page; a full page triggers allocation of a
// new one. Deletes are tombstones, so a RID never moves once handed out.
class HeapFile {
 public:
  HeapFile(BufferPool& buffer_pool, PageId first_page);

  RID  insert(const std::vector<char>& tuple_bytes);
  std::vector<char> get(RID rid) const;
  bool erase(RID rid);

  PageId first_page() const { return first_page_; }

  // Forward cursor over every live (non-tombstoned) tuple in the chain.
  class Cursor {
   public:
    Cursor(BufferPool& buffer_pool, PageId start);
    bool next(RID& out_rid, std::vector<char>& out_bytes);

   private:
    BufferPool& buffer_pool_;
    PageId      page_id_;
    uint16_t    slot_ = 0;
  };
  Cursor scan() const { return Cursor(buffer_pool_, first_page_); }

 private:
  BufferPool& buffer_pool_;
  PageId      first_page_;
  PageId      last_page_;  // tail of the chain, for O(1) append
};

}  // namespace minidb
