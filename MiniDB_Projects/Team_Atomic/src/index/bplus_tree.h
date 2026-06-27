#pragma once
#include <vector>
#include <utility>
#include "common/types.h"
#include "storage/buffer_pool_manager.h"

namespace minidb {

// A page-backed B+ tree mapping int64 key -> RID, used as the primary-key
// index. A header page (id stored in the catalog) holds the current root id,
// so the tree survives restarts and root splits.
//
// Node format:
//   Leaf:     type(4) size(4) next(4) | [ key(8) page(4) slot(4) ] * size
//   Internal: type(4) size(4)         | child(4)*(size+1) | key(8)*size
class BPlusTree {
 public:
  // Max keys per node before a split (fits in a 4 KB page).
  static constexpr int LEAF_MAX = 254;
  static constexpr int INTERNAL_MAX = 254;

  BPlusTree(BufferPoolManager* bpm, page_id_t header_page_id)
      : bpm_(bpm), header_page_id_(header_page_id) {}

  // Allocate a fresh, empty index; returns its header page id (store in catalog).
  static page_id_t Create(BufferPoolManager* bpm);

  // Point lookup. Returns true and sets *out if `key` is present.
  bool Search(int64_t key, RID* out);

  // Insert key->rid. Overwrites the RID if the key already exists.
  void Insert(int64_t key, const RID& rid);

  // Remove `key` if present (leaf entry removed; no rebalance -- see README).
  bool Delete(int64_t key);

  // Range scan: all RIDs with low <= key <= high, in key order.
  std::vector<std::pair<int64_t, RID>> Range(int64_t low, int64_t high);

  bool IsEmpty() { return GetRoot() == INVALID_PAGE_ID; }

 private:
  enum NodeType : int32_t { kInternal = 0, kLeaf = 1 };

  struct LeafNode {
    page_id_t self = INVALID_PAGE_ID;
    page_id_t next = INVALID_PAGE_ID;
    std::vector<std::pair<int64_t, RID>> entries;  // sorted by key
  };
  struct InternalNode {
    page_id_t self = INVALID_PAGE_ID;
    std::vector<int64_t> keys;       // size = n
    std::vector<page_id_t> children; // size = n + 1
  };

  page_id_t GetRoot();
  void SetRoot(page_id_t root);

  NodeType TypeOf(page_id_t pid);
  LeafNode ReadLeaf(page_id_t pid);
  InternalNode ReadInternal(page_id_t pid);
  void WriteLeaf(const LeafNode& n);
  void WriteInternal(const InternalNode& n);
  page_id_t NewNode(NodeType type, LeafNode* leaf, InternalNode* internal);

  // Descend to the leaf that should contain `key`, recording the path of
  // internal nodes traversed (for split propagation).
  page_id_t FindLeaf(int64_t key, std::vector<page_id_t>* path);

  // Insert (sep_key, right_child) into parent internal node `pid`, splitting up.
  void InsertIntoParent(std::vector<page_id_t>& path, int64_t sep_key,
                        page_id_t left, page_id_t right);

  BufferPoolManager* bpm_;
  page_id_t header_page_id_;
};

}  // namespace minidb
