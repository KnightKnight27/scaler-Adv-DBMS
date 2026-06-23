// btree.cpp — Track 3 (Query & Concurrency)
#include "btree.h"

#include <algorithm>

namespace minidb {

// A single node. We keep one struct for both leaves and internal nodes so the
// split logic never has to downcast. For a leaf, `rids`/`deleted` run parallel
// to `keys` and `next` chains leaves left-to-right. For an internal node,
// `children` has size keys.size()+1.
struct Node {
  bool is_leaf;
  std::vector<Key> keys;
  std::vector<RID> rids;      // leaf only
  std::vector<char> deleted;  // leaf only (tombstones)
  std::vector<Node*> children;  // internal only
  Node* next = nullptr;         // leaf only

  explicit Node(bool leaf) : is_leaf(leaf) {}
};

BPlusTree::~BPlusTree() { freeRec(root_); }

void BPlusTree::freeRec(Node* node) {
  if (!node) return;
  if (!node->is_leaf) {
    for (Node* c : node->children) freeRec(c);
  }
  delete node;
}

Node* BPlusTree::findLeaf(Key key) const {
  Node* node = root_;
  while (node && !node->is_leaf) {
    // First child whose separator is > key.
    int idx = static_cast<int>(
        std::upper_bound(node->keys.begin(), node->keys.end(), key) -
        node->keys.begin());
    node = node->children[idx];
  }
  return node;
}

std::optional<RID> BPlusTree::search(Key key) const {
  Node* leaf = findLeaf(key);
  if (!leaf) return std::nullopt;
  int i = static_cast<int>(
      std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key) -
      leaf->keys.begin());
  for (; i < static_cast<int>(leaf->keys.size()) && leaf->keys[i] == key; ++i) {
    if (!leaf->deleted[i]) return leaf->rids[i];
  }
  return std::nullopt;
}

bool BPlusTree::remove(Key key) {
  Node* leaf = findLeaf(key);
  if (!leaf) return false;
  int i = static_cast<int>(
      std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key) -
      leaf->keys.begin());
  for (; i < static_cast<int>(leaf->keys.size()) && leaf->keys[i] == key; ++i) {
    if (!leaf->deleted[i]) {
      leaf->deleted[i] = 1;  // tombstone, no rebalancing
      return true;
    }
  }
  return false;
}

BPlusTree::SplitResult BPlusTree::insertRec(Node* node, Key key, RID rid) {
  if (node->is_leaf) {
    int pos = static_cast<int>(
        std::lower_bound(node->keys.begin(), node->keys.end(), key) -
        node->keys.begin());
    node->keys.insert(node->keys.begin() + pos, key);
    node->rids.insert(node->rids.begin() + pos, rid);
    node->deleted.insert(node->deleted.begin() + pos, 0);

    if (static_cast<int>(node->keys.size()) <= kOrder) return {};

    // Split leaf: right half moves to a new leaf; its first key is copied up.
    int mid = static_cast<int>(node->keys.size()) / 2;
    Node* right = new Node(true);
    right->keys.assign(node->keys.begin() + mid, node->keys.end());
    right->rids.assign(node->rids.begin() + mid, node->rids.end());
    right->deleted.assign(node->deleted.begin() + mid, node->deleted.end());
    node->keys.resize(mid);
    node->rids.resize(mid);
    node->deleted.resize(mid);

    right->next = node->next;
    node->next = right;
    return {true, right->keys.front(), right};
  }

  // Internal: descend into the right child, then absorb any split it returns.
  int idx = static_cast<int>(
      std::upper_bound(node->keys.begin(), node->keys.end(), key) -
      node->keys.begin());
  SplitResult child = insertRec(node->children[idx], key, rid);
  if (!child.did_split) return {};

  node->keys.insert(node->keys.begin() + idx, child.sep_key);
  node->children.insert(node->children.begin() + idx + 1, child.new_right);

  if (static_cast<int>(node->keys.size()) <= kOrder) return {};

  // Split internal: the middle key moves up (it is NOT kept in either child).
  int mid = static_cast<int>(node->keys.size()) / 2;
  Key sep = node->keys[mid];
  Node* right = new Node(false);
  right->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
  right->children.assign(node->children.begin() + mid + 1,
                         node->children.end());
  node->keys.resize(mid);
  node->children.resize(mid + 1);
  return {true, sep, right};
}

void BPlusTree::insert(Key key, RID rid) {
  if (!root_) {
    root_ = new Node(true);
    root_->keys.push_back(key);
    root_->rids.push_back(rid);
    root_->deleted.push_back(0);
    return;
  }
  SplitResult s = insertRec(root_, key, rid);
  if (s.did_split) {
    Node* new_root = new Node(false);
    new_root->keys.push_back(s.sep_key);
    new_root->children.push_back(root_);
    new_root->children.push_back(s.new_right);
    root_ = new_root;
  }
}

// ---- Iterator ----

void BPlusTree::Iterator::skipToValid() {
  while (leaf_) {
    if (idx_ >= static_cast<int>(leaf_->keys.size())) {
      leaf_ = leaf_->next;
      idx_ = 0;
      continue;
    }
    if (leaf_->keys[idx_] > high_) {  // past the requested range
      leaf_ = nullptr;
      return;
    }
    if (leaf_->deleted[idx_]) {  // skip tombstones
      ++idx_;
      continue;
    }
    return;  // landed on a live, in-range entry
  }
}

Key BPlusTree::Iterator::key() const { return leaf_->keys[idx_]; }
RID BPlusTree::Iterator::rid() const { return leaf_->rids[idx_]; }

void BPlusTree::Iterator::next() {
  ++idx_;
  skipToValid();
}

BPlusTree::Iterator BPlusTree::range(Key low, Key high) const {
  Iterator it;
  it.high_ = high;
  Node* leaf = findLeaf(low);
  if (!leaf) return it;  // empty tree -> invalid iterator
  it.leaf_ = leaf;
  it.idx_ = static_cast<int>(
      std::lower_bound(leaf->keys.begin(), leaf->keys.end(), low) -
      leaf->keys.begin());
  it.skipToValid();
  return it;
}

}  // namespace minidb
