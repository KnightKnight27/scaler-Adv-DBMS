#pragma once
#include <optional>
#include <utility>
#include <vector>
#include "common/config.h"
#include "common/types.h"

namespace minidb {

// An in-memory B+Tree mapping a primary Key to the RID of its row in the heap
// file. Internal nodes route searches; all keys live in leaves, which are
// linked left-to-right so range scans walk sideways. The tree is rebuilt from a
// heap scan when a table is opened, so it is a fast lookup structure rather than
// a durable one.
//
// Insert performs the full split-and-propagate algorithm. Delete is "lazy": it
// removes the key from its leaf but does not merge/borrow under-full nodes. The
// tree stays correct for search and range scans (separator keys are only
// routing hints); it can just become less densely packed. See README limits.
class BPlusTree {
 public:
  explicit BPlusTree(int order = BTREE_ORDER);
  ~BPlusTree();

  void insert(Key key, RID rid);   // insert, or overwrite the RID if key exists
  bool erase(Key key);             // remove key from its leaf; false if absent
  std::optional<RID> search(Key key) const;
  std::vector<std::pair<Key, RID>> range(Key lo, Key hi) const;  // inclusive
  void clear();

 private:
  struct Node {
    bool               leaf;
    std::vector<Key>   keys;
    std::vector<Node*> children;  // internal nodes only
    std::vector<RID>   rids;       // leaf nodes only (parallel to keys)
    Node*              next = nullptr;  // next leaf in the chain
    explicit Node(bool is_leaf) : leaf(is_leaf) {}
  };

  // Result of inserting into a subtree: if it overflowed and split, the caller
  // must absorb the new separator key and the new right sibling.
  struct Split {
    bool  happened = false;
    Key   sep      = 0;
    Node* right    = nullptr;
  };

  Split  insert_into(Node* node, Key key, RID rid);
  Node*  find_leaf(Key key) const;             // descend to the leaf for key
  void   destroy(Node* node);

  int    max_keys() const { return order_ - 1; }

  int   order_;
  Node* root_ = nullptr;
};

}  // namespace minidb
