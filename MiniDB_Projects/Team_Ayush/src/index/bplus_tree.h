#pragma once
#include <cstdint>
#include <functional>
#include "common/types.h"
#include "storage/buffer_pool.h"

namespace minidb {

// A disk-resident B+Tree mapping an INT key (typically a primary key) to a RID.
// Every node is one buffer-pool page. A small header page stores the current
// root page id so that root changes (from splits) are persisted automatically.
//
// Design notes / simplifications (documented limitations):
//  * Keys are unique; inserting an existing key overwrites its value.
//  * Deletion is "lazy": the entry is removed from its leaf but nodes are never
//    merged/rebalanced. Searches stay correct; the tree may stay taller than
//    strictly necessary after many deletes. This is a deliberate MVP trade-off.
//  * Leaves are linked left-to-right to support ordered range scans.
class BPlusTree {
 public:
  // Create an empty tree (header page + empty leaf root). Returns header page id.
  static PageId CreateNew(BufferPool* bp);

  BPlusTree(BufferPool* bp, PageId header_page)
      : bp_(bp), header_page_(header_page) {}

  // Point lookup. Returns true and sets *out if found.
  bool Search(int32_t key, RID* out);

  // Insert or overwrite key -> value.
  void Insert(int32_t key, RID value);

  // Remove key (lazy). Returns true if it existed.
  bool Delete(int32_t key);

  // Visit all (key, rid) with low <= key <= high, in ascending key order.
  void Range(int32_t low, int32_t high,
             const std::function<void(int32_t, RID)>& visitor);

  // Visit every (key, rid) in ascending key order.
  void ScanAll(const std::function<void(int32_t, RID)>& visitor);

 private:
  struct InsertResult {
    bool    split;    // did the child split, promoting a key?
    int32_t up_key;   // separator key to insert into the parent
    PageId  right;    // new right sibling page id
  };

  PageId Root();
  void   SetRoot(PageId r);
  PageId LeftmostLeaf();
  InsertResult InsertRec(PageId pid, int32_t key, RID value);

  BufferPool* bp_;
  PageId      header_page_;
};

}  // namespace minidb
