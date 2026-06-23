#pragma once
// In-memory B+Tree index over Value keys -> RIDs.
//
// Real B+Tree mechanics: internal/leaf nodes with order kOrder, leaf splitting
// propagated up the tree, and a linked list of leaves for ordered range scans.
// The index is held in memory and rebuilt by scanning the table on open
// (durable data lives in the heap); see HeapEngine.
//
// Keys may repeat: each distinct key maps to a vector of RIDs, so non-unique
// indexes work. Deletion erases a specific (key, rid) pair; nodes are not merged
// on underflow (tolerated for this educational scope).
#include "minidb/storage/page.hpp"
#include "minidb/storage/storage_engine.hpp"
#include "minidb/types.hpp"
#include <cstddef>
#include <memory>
#include <vector>

namespace minidb {

struct BPlusNode;  // implementation detail, defined in index.cpp

class BPlusTree {
 public:
  BPlusTree();
  ~BPlusTree();
  BPlusTree(BPlusTree&&) noexcept;
  BPlusTree& operator=(BPlusTree&&) noexcept;
  BPlusTree(const BPlusTree&) = delete;
  BPlusTree& operator=(const BPlusTree&) = delete;

  // Adds (key, rid). Duplicate keys accumulate RIDs.
  void insert(const Value& key, const RID& rid);
  // Removes one (key, rid) pair. Returns false if not present.
  bool remove(const Value& key, const RID& rid);
  // All RIDs for an exact key (empty if absent).
  std::vector<RID> find(const Value& key) const;
  // All RIDs whose key falls in the bound, in ascending key order.
  std::vector<RID> range(const Value& lo, bool lo_incl, const Value& hi, bool hi_incl) const;
  // Total number of (key, rid) entries.
  size_t size() const { return count_; }

 private:
  std::unique_ptr<BPlusNode> root_;
  size_t count_ = 0;
  static const size_t kOrder = 64;  // max keys per node before a split
};

// Backwards-compatible name used elsewhere in the tree.
using BPlusTreeIndex = BPlusTree;

}  // namespace minidb
