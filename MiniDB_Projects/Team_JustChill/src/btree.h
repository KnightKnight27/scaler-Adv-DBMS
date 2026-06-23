// btree.h — Phase B: page-backed primary-key B+ Tree (int64 key -> RID).
//
// Every node is exactly one page, stored through the same BufferPool/HeapFile
// as the rest of the engine, so the index is durable and survives restarts.
// The root page id lives in a meta page (page 0) so it can be recovered.
//
// Supports point Search, range scans (leaf-linked Iterator, used by IndexScan)
// and Insert with node splits. Per the project brief we SKIP rebalancing on
// delete: remove() tombstones a leaf entry (its RID page_id is set to a
// sentinel) and searches/scans skip it — no merge.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// Storage backends live in the global namespace (page.h / buffer_pool.h).
class BufferPool;
class HeapFile;

namespace minidb {

// Record identifier: (page_id, slot_id) into the page-backed heap table.
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
  // Max keys per node. A leaf holds up to 64×(8B key + 6B RID) ≈ 1 KB and an
  // internal node up to 64×8B keys + 65×4B children ≈ 780 B — both fit a 4 KB
  // page with room for the temporary (kOrder+1) overflow before a split.
  static constexpr int kOrder = 64;

  BPlusTree() = default;
  ~BPlusTree() = default;  // pages are owned by the BufferPool, not by us

  BPlusTree(const BPlusTree&) = delete;
  BPlusTree& operator=(const BPlusTree&) = delete;

  // Bind the tree to its page storage. `fresh` creates a new empty tree (meta
  // page + one empty leaf); otherwise the root page id is read back from the
  // meta page. `fresh` requires a truncated/empty file (the caller guarantees
  // this — see Table).
  void open(::BufferPool* pool, ::HeapFile* heap, bool fresh);

  // Insert a key/RID pair (duplicate keys allowed; uniqueness is enforced above
  // this layer). Splits overflowing nodes and grows a new root as needed.
  void insert(Key key, RID rid);

  // Point lookup: the RID for `key`, or nullopt if absent / tombstoned.
  std::optional<RID> search(Key key) const;

  // Tombstone the entry for `key` (no rebalancing). True if a live entry was
  // found and marked deleted.
  bool remove(Key key);

  // Forward iterator over live entries in [low, high]. It copies one leaf's
  // entries at a time and follows leaf `next` page ids, so it never holds a
  // page pinned across calls.
  class Iterator {
   public:
    bool valid() const { return valid_; }
    Key key() const { return keys_[idx_]; }
    RID rid() const { return rids_[idx_]; }
    void next();

   private:
    friend class BPlusTree;
    const BPlusTree* tree_ = nullptr;
    std::vector<Key> keys_;
    std::vector<RID> rids_;
    int next_leaf_ = -1;
    int idx_ = 0;
    Key high_ = 0;
    bool valid_ = false;
    void loadLeaf(int page_id);
    void skipToValid();
  };

  static constexpr Key kMin = INT64_MIN;
  static constexpr Key kMax = INT64_MAX;

  Iterator range(Key low = kMin, Key high = kMax) const;

 private:
  struct SplitResult {
    bool did_split = false;
    Key sep_key = 0;     // separator pushed up to the parent
    int new_right = -1;  // page id of the new right sibling
  };

  SplitResult insertRec(int page_id, Key key, RID rid);
  int findLeafPage(Key key) const;
  void readRootFromMeta();
  void writeRootToMeta();

  ::BufferPool* pool_ = nullptr;
  ::HeapFile* heap_ = nullptr;
  int root_page_id_ = -1;
};

}  // namespace minidb
