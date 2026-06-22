// ============================================================================
// bplus_tree.h  --  A B+ Tree that maps a key (a Value) to a RID.
//
// WHY A B+ TREE?  A heap scan is O(n): to find one row you read every page.  An
// index gives O(log n) lookups and, crucially, *ordered* access so range
// queries (age > 30) and sorted output are cheap.  The B+ Tree is the workhorse
// index of almost every relational database.
//
// SHAPE OF THE TREE:
//   * Internal nodes hold only keys + child pointers; they are signposts.
//   * Leaf nodes hold the actual (key -> RID) entries and are chained left to
//     right, so a range scan is "find the start leaf, then walk the chain".
//   * All leaves are at the same depth (the tree stays balanced) because growth
//     happens by SPLITTING a full node and pushing one key up to the parent.
//
// DESIGN CHOICES (kept deliberately simple so we can explain every line):
//   * The tree lives in memory.  On startup the database rebuilds it by scanning
//     the heap, so it never needs its own on-disk format or crash recovery.
//   * Keys MAY repeat, so the same structure works for a non-unique secondary
//     index as well as a unique primary key.  Because of that, remove() takes
//     the exact RID to delete (so it removes the right row when keys collide).
//   * insert does full splitting (the interesting, must-understand operation).
//     Delete simply removes the entry from its leaf; we tolerate empty/underfull
//     nodes rather than merging them.  This is valid because a B+ Tree's internal
//     keys are only *separators* (they need not exist as real entries), so
//     lookups stay correct.  The cost is some wasted space (noted in Limitations).
// ============================================================================
#pragma once

#include "common/common.h"
#include "record/value.h"

namespace minidb {

class BPlusTree {
 public:
  // `order` = maximum number of keys a node may hold before it must split.
  explicit BPlusTree(int order = 4) : order_(order) {}
  ~BPlusTree() { destroy(root_); }

  // insert a (key -> rid) entry.  Duplicate keys are allowed (kept side by side).
  void insert(const Value &key, const RID &rid);

  // Point lookup: writes the RID of the FIRST entry with this key and returns
  // true if any exists.  (Used to test primary-key existence.)
  bool search(const Value &key, RID *out) const;

  // remove the specific (key, rid) entry (no-op if absent).
  void remove(const Value &key, const RID &rid);

  // range scan: all RIDs whose key is in [low, high].  A null bound means
  // "unbounded on that side".  Results come out in ascending key order.
  vector<RID> range(const Value *low, const Value *high) const;

  bool empty() const { return root_ == nullptr; }

 private:
  // One node of the tree.  Internal nodes use `children`; leaves use `rids` and
  // the `next` chain pointer.
  struct Node {
    bool                 is_leaf;
    vector<Value>   keys;
    vector<Node *>  children;  // internal only; size == keys.size()+1
    vector<RID>     rids;      // leaf only;     size == keys.size()
    Node                *next{nullptr};  // leaf-chain link to the next leaf
    explicit Node(bool leaf) : is_leaf(leaf) {}
  };

  // Result of inserting into a subtree: if the child split, it reports the key
  // promoted up and the new right sibling so the parent can absorb them.
  struct InsertResult {
    bool   split{false};
    Value  up_key;
    Node  *right{nullptr};
  };

  InsertResult insertInto(Node *node, const Value &key, const RID &rid);
  Node *findLeaf(const Value &key) const;   // descend to the leaf that owns key
  void destroy(Node *node);

  Node *root_{nullptr};
  int   order_;
};

}  // namespace minidb
