#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "buffer/buffer_pool.h"
#include "common/config.h"
#include "common/rid.h"

namespace walterdb {

// ===========================================================================
// BPlusTree -- a disk-resident B+tree mapping byte-string keys to RIDs.
//
// Used as a table's primary-key index: the relational layer encodes a column
// value into an order-preserving key (see Value::encode_key) and stores
// key -> RID, so a point lookup is search() and a range predicate is a leaf
// scan.  It also backs the KV HeapBTreeEngine's ordered get/scan.
//
// Design choices (all deliberate, for a 1-2 day scope):
//   * FIXED fanout with a bounded key length (MAX_KEY_LEN).  Variable-length
//     slotted nodes are more space-efficient but materially trickier to split
//     correctly; fixed-stride arrays make split/redistribute obviously correct.
//     Keys longer than MAX_KEY_LEN are rejected (int keys are 9 B; this only
//     limits very long string primary keys).
//   * Insert is an UPSERT: re-inserting an existing key overwrites its RID
//     (matches KV put semantics and keeps the PK index unique).
//   * Delete is LAZY: the key is removed from its leaf with NO merge / rebalance
//     on underflow.  Full B+tree deletion (merge/redistribute) is the single
//     biggest time sink and correctness risk in the project, and search/range
//     correctness does not depend on it -- a stated, defensible trade-off.
//   * The current root page id lives in a stable META page, so the tree's
//     identity (meta_page_id, stored in the catalog) survives root splits and
//     reopens.
// ===========================================================================

class BPlusTree {
 public:
  static constexpr size_t MAX_KEY_LEN = 64;

  // Open an existing tree given its stable meta-page id.
  BPlusTree(BufferPool* bpm, page_id_t meta_page_id);

  // Create a new, empty tree.  meta_page_id() is what the catalog persists.
  static BPlusTree create(BufferPool* bpm);

  page_id_t meta_page_id() const { return meta_page_id_; }

  // Upsert key -> rid.  Returns true if a new key was inserted, false if an
  // existing key's RID was overwritten.  Throws if key.size() > MAX_KEY_LEN.
  bool insert(std::string_view key, RID rid);

  // Point lookup.
  std::optional<RID> search(std::string_view key) const;

  // Lazy delete (no merge).  Returns true if the key was present and removed.
  bool erase(std::string_view key);

  // Forward cursor over [lo, hi) in key order.  Empty lo => from the start;
  // empty hi => to the end.  Holds one leaf pinned at a time; move-only.
  class Iterator {
   public:
    Iterator() = default;
    Iterator(BufferPool* bpm, page_id_t leaf, int idx, std::string hi);
    ~Iterator();
    Iterator(Iterator&&) noexcept;
    Iterator& operator=(Iterator&&) noexcept;
    Iterator(const Iterator&) = delete;
    Iterator& operator=(const Iterator&) = delete;

    bool valid() const { return page_ != nullptr; }
    std::string_view key() const { return key_; }
    RID rid() const { return rid_; }
    void next();

   private:
    void load_current();   // read key_/rid_ at (page_,idx_) or invalidate
    void release();

    BufferPool* bpm_ = nullptr;
    page_id_t page_id_ = INVALID_PAGE_ID;
    class Page* page_ = nullptr;  // pinned while non-null
    int idx_ = 0;
    std::string hi_;              // exclusive upper bound ("" = unbounded)
    std::string key_;
    RID rid_{};
  };

  Iterator range(std::string_view lo, std::string_view hi) const;
  Iterator begin() const { return range({}, {}); }

  // Diagnostics (used by tests and the optimizer's height-based cost model).
  int height() const;

 private:
  struct SplitResult {
    std::string sep_key;       // separator pushed up to the parent
    page_id_t right_page;      // newly created right sibling
  };

  page_id_t root_page_id() const;
  void set_root_page_id(page_id_t pid);

  // Descend to the leaf that would contain `key`, returning its page id.
  page_id_t find_leaf(std::string_view key) const;

  // Recursive insert; returns a split to propagate, or nullopt.
  std::optional<SplitResult> insert_rec(page_id_t node_pid, std::string_view key,
                                        RID rid, bool* inserted_new);

  BufferPool* bpm_;
  page_id_t meta_page_id_;
};

}  // namespace walterdb
