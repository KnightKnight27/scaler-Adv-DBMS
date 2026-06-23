#pragma once
#include <utility>
#include <vector>
#include "common/types.h"
#include "storage/buffer_pool_manager.h"

namespace minidb {

// A page-backed B+ tree mapping an int64 primary key -> RID. Each node lives in
// one buffer-pool page; the current root page id is kept in a durable header
// page (whose id is recorded in the catalog) so the tree survives restart and
// root splits.
//
// Node capacities are chosen to fit comfortably inside a 4 KB page.
class BPlusTree {
 public:
  static constexpr int LEAF_MAX     = 250;  // max (key,RID) entries per leaf
  static constexpr int INTERNAL_MAX = 250;  // max separator keys per internal

  // Open the tree whose header is *header_page_id, or create a fresh empty tree
  // (pass INVALID_PAGE_ID; *header_page_id is updated to the new header).
  BPlusTree(BufferPoolManager *bpm, page_id_t *header_page_id);

  // Insert key->rid. Returns false if the key already exists (PK uniqueness).
  bool Insert(int64_t key, const RID &rid);

  // Point lookup. Returns true and sets *out if found.
  bool GetValue(int64_t key, RID *out);

  // Remove a key (leaf removal; underflow nodes are not merged — see DESIGN).
  bool Delete(int64_t key);

  // Inclusive range scan [low, high], in key order, via the leaf chain.
  std::vector<std::pair<int64_t, RID>> Range(int64_t low, int64_t high);

  page_id_t header_page_id() const { return header_page_id_; }

 private:
  page_id_t GetRoot();
  void      SetRoot(page_id_t root);

  // Descend from the root to the leaf that should contain `key`, recording the
  // internal-node path (for split propagation).
  page_id_t FindLeaf(int64_t key, std::vector<page_id_t> *path);

  // Insert a (key,child) separator into an internal node, splitting if needed
  // and propagating up the recorded path; grows a new root when the root splits.
  void InsertIntoParent(std::vector<page_id_t> &path, int64_t key,
                        page_id_t left_child, page_id_t right_child);

  BufferPoolManager *bpm_;
  page_id_t          header_page_id_;
};

}  // namespace minidb
