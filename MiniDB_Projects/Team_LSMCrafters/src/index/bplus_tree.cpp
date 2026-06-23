#include "index/bplus_tree.h"
#include <algorithm>

namespace minidb {

BPlusTree::BPlusTree(int order) : order_(order < 3 ? 3 : order) {}
BPlusTree::~BPlusTree() { destroy(root_); }

void BPlusTree::destroy(Node* node) {
  if (!node) return;
  if (!node->leaf) {
    for (Node* c : node->children) destroy(c);
  }
  delete node;
}

void BPlusTree::clear() {
  destroy(root_);
  root_ = nullptr;
}

// Child index to follow for `key`: the first separator strictly greater than
// key (equal keys live in the right child, since a separator is the right
// child's minimum key).
static int child_index(const std::vector<Key>& keys, Key key) {
  return static_cast<int>(std::upper_bound(keys.begin(), keys.end(), key) - keys.begin());
}

BPlusTree::Split BPlusTree::insert_into(Node* node, Key key, RID rid) {
  if (node->leaf) {
    int i = static_cast<int>(std::lower_bound(node->keys.begin(), node->keys.end(), key) -
                             node->keys.begin());
    if (i < static_cast<int>(node->keys.size()) && node->keys[i] == key) {
      node->rids[i] = rid;  // overwrite existing key
      return {};
    }
    node->keys.insert(node->keys.begin() + i, key);
    node->rids.insert(node->rids.begin() + i, rid);
    if (static_cast<int>(node->keys.size()) <= max_keys()) return {};

    // Split the leaf: the right half moves to a new leaf, its first key is
    // copied up as the separator.
    int mid = static_cast<int>(node->keys.size()) / 2;
    Node* right = new Node(true);
    right->keys.assign(node->keys.begin() + mid, node->keys.end());
    right->rids.assign(node->rids.begin() + mid, node->rids.end());
    node->keys.resize(mid);
    node->rids.resize(mid);
    right->next = node->next;
    node->next  = right;
    return {true, right->keys.front(), right};
  }

  int ci = child_index(node->keys, key);
  Split child = insert_into(node->children[ci], key, rid);
  if (!child.happened) return {};

  node->keys.insert(node->keys.begin() + ci, child.sep);
  node->children.insert(node->children.begin() + ci + 1, child.right);
  if (static_cast<int>(node->keys.size()) <= max_keys()) return {};

  // Split the internal node: the median key moves up (not copied).
  int mid  = static_cast<int>(node->keys.size()) / 2;
  Key sep  = node->keys[mid];
  Node* right = new Node(false);
  right->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
  right->children.assign(node->children.begin() + mid + 1, node->children.end());
  node->keys.resize(mid);
  node->children.resize(mid + 1);
  return {true, sep, right};
}

void BPlusTree::insert(Key key, RID rid) {
  if (!root_) {
    root_ = new Node(true);
    root_->keys.push_back(key);
    root_->rids.push_back(rid);
    return;
  }
  Split s = insert_into(root_, key, rid);
  if (s.happened) {
    Node* new_root = new Node(false);
    new_root->keys.push_back(s.sep);
    new_root->children.push_back(root_);
    new_root->children.push_back(s.right);
    root_ = new_root;
  }
}

BPlusTree::Node* BPlusTree::find_leaf(Key key) const {
  Node* node = root_;
  while (node && !node->leaf) node = node->children[child_index(node->keys, key)];
  return node;
}

std::optional<RID> BPlusTree::search(Key key) const {
  Node* leaf = find_leaf(key);
  if (!leaf) return std::nullopt;
  auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
  if (it != leaf->keys.end() && *it == key)
    return leaf->rids[it - leaf->keys.begin()];
  return std::nullopt;
}

std::vector<std::pair<Key, RID>> BPlusTree::range(Key lo, Key hi) const {
  std::vector<std::pair<Key, RID>> out;
  Node* leaf = find_leaf(lo);
  while (leaf) {
    for (std::size_t i = 0; i < leaf->keys.size(); ++i) {
      if (leaf->keys[i] < lo) continue;
      if (leaf->keys[i] > hi) return out;
      out.emplace_back(leaf->keys[i], leaf->rids[i]);
    }
    leaf = leaf->next;
  }
  return out;
}

bool BPlusTree::erase(Key key) {
  Node* leaf = find_leaf(key);
  if (!leaf) return false;
  auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
  if (it == leaf->keys.end() || *it != key) return false;
  int i = static_cast<int>(it - leaf->keys.begin());
  leaf->keys.erase(leaf->keys.begin() + i);
  leaf->rids.erase(leaf->rids.begin() + i);
  return true;
}

}  // namespace minidb
