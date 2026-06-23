// btree.h — Track 3 (Query & Concurrency)
//
// Primary-key B+ Tree index mapping an int64 key -> RID (record id).
// Supports point Search, range scans (via leaf-linked Iterator, used by the
// IndexScan operator) and Insert with node splits.
//
// Per the track brief we SKIP rebalancing on delete: remove() just sets a
// tombstone (is_deleted) flag on the leaf entry. Searches and scans skip
// tombstoned entries, so deleted keys disappear logically without any merge.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace minidb {

// Record identifier. With the in-memory storage used by the executor we set
// page_id = row index and slot_id = 0; the heap-file track can later use the
// real (page, slot) pair without changing this struct.
struct RID {
  uint32_t page_id = 0;
  uint16_t slot_id = 0;

  bool operator==(const RID& o) const {
    return page_id == o.page_id && slot_id == o.slot_id;
  }
};

using Key = int64_t;

class BPlusTree {
 public:
  // Max keys per node. Small enough to exercise splits in tests, large enough
  // to stay shallow for the benchmarks.
  static constexpr int kOrder = 64;

  BPlusTree() = default;
  ~BPlusTree();

  BPlusTree(const BPlusTree&) = delete;
  BPlusTree& operator=(const BPlusTree&) = delete;

  // Insert a key/RID pair. Duplicate keys are allowed (a re-inserted PK simply
  // shadows the old one on search); primary-key uniqueness is enforced above
  // this layer.
  void insert(Key key, RID rid);

  // Point lookup. Returns the RID for `key`, or nullopt if absent/tombstoned.
  std::optional<RID> search(Key key) const;

  // Tombstone the entry for `key` (no rebalancing). Returns true if a live
  // entry was found and marked deleted.
  bool remove(Key key);

  // Forward iterator over live entries in [low, high]. Use kMin/kMax for an
  // unbounded end. Walks the leaf linked list, skipping tombstones.
  class Iterator {
   public:
    bool valid() const { return leaf_ != nullptr; }
    Key key() const;
    RID rid() const;
    void next();  // advance to the next live, in-range entry

   private:
    friend class BPlusTree;
    struct Node* leaf_ = nullptr;
    int idx_ = 0;
    Key high_ = 0;
    void skipToValid();
  };

  static constexpr Key kMin = INT64_MIN;
  static constexpr Key kMax = INT64_MAX;

  // Begin a scan over [low, high].
  Iterator range(Key low = kMin, Key high = kMax) const;

 private:
  struct SplitResult {
    bool did_split = false;
    Key sep_key = 0;       // separator pushed up to the parent
    Node* new_right = nullptr;
  };

  SplitResult insertRec(Node* node, Key key, RID rid);
  Node* findLeaf(Key key) const;
  static void freeRec(Node* node);

  Node* root_ = nullptr;
};

}  // namespace minidb
