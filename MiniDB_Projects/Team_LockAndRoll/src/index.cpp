#include "index.h"

#include <algorithm>

namespace minidb {

void BPlusTree::destroy(Node* n) {
  if (!n) return;
  if (!n->leaf)
    for (Node* c : n->children) destroy(c);
  delete n;
}

int BPlusTree::height() const {
  int h = 0;
  for (Node* n = root_; n; n = n->leaf ? nullptr : n->children[0]) h++;
  return h;
}

BPlusTree::Node* BPlusTree::find_leaf(int64_t key) const {
  Node* n = root_;
  while (n && !n->leaf) {
    auto it = std::upper_bound(n->keys.begin(), n->keys.end(), key);
    int ci = static_cast<int>(it - n->keys.begin());
    n = n->children[ci];
  }
  return n;
}

std::optional<RID> BPlusTree::search(int64_t key) const {
  Node* leaf = find_leaf(key);
  if (!leaf) return std::nullopt;
  auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
  if (it != leaf->keys.end() && *it == key)
    return leaf->vals[it - leaf->keys.begin()];
  return std::nullopt;
}

void BPlusTree::range_scan(int64_t lo, int64_t hi,
                           const std::function<void(int64_t, RID)>& fn) const {
  Node* leaf = find_leaf(lo);
  while (leaf) {
    for (size_t i = 0; i < leaf->keys.size(); i++) {
      if (leaf->keys[i] < lo) continue;
      if (leaf->keys[i] > hi) return;
      fn(leaf->keys[i], leaf->vals[i]);
    }
    leaf = leaf->next;
  }
}

bool BPlusTree::insert(int64_t key, RID rid) {
  if (!root_) root_ = new Node(true);
  bool inserted = false;
  SplitInfo s = insert_rec(root_, key, rid, &inserted);
  if (s.split) {
    Node* nr = new Node(false);
    nr->keys.push_back(s.key);
    nr->children.push_back(root_);
    nr->children.push_back(s.right);
    root_ = nr;
  }
  if (inserted) count_++;
  return inserted;
}

BPlusTree::SplitInfo BPlusTree::insert_rec(Node* node, int64_t key, RID rid, bool* inserted) {
  SplitInfo out;
  if (node->leaf) {
    auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
    int pos = static_cast<int>(it - node->keys.begin());
    if (it != node->keys.end() && *it == key) {
      *inserted = false;
      return out;
    }
    node->keys.insert(node->keys.begin() + pos, key);
    node->vals.insert(node->vals.begin() + pos, rid);
    *inserted = true;
    if (static_cast<int>(node->keys.size()) > MAX_KEYS) {
      int mid = node->keys.size() / 2;
      Node* right = new Node(true);
      right->keys.assign(node->keys.begin() + mid, node->keys.end());
      right->vals.assign(node->vals.begin() + mid, node->vals.end());
      node->keys.resize(mid);
      node->vals.resize(mid);
      right->next = node->next;
      node->next = right;
      out.split = true;
      out.key = right->keys.front();  // leaf split copies the key up
      out.right = right;
    }
    return out;
  }
  auto it = std::upper_bound(node->keys.begin(), node->keys.end(), key);
  int ci = static_cast<int>(it - node->keys.begin());
  SplitInfo cs = insert_rec(node->children[ci], key, rid, inserted);
  if (cs.split) {
    node->keys.insert(node->keys.begin() + ci, cs.key);
    node->children.insert(node->children.begin() + ci + 1, cs.right);
    if (static_cast<int>(node->keys.size()) > MAX_KEYS) {
      int mid = node->keys.size() / 2;
      int64_t up = node->keys[mid];  // internal split pushes the key up
      Node* right = new Node(false);
      right->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
      right->children.assign(node->children.begin() + mid + 1, node->children.end());
      node->keys.resize(mid);
      node->children.resize(mid + 1);
      out.split = true;
      out.key = up;
      out.right = right;
    }
  }
  return out;
}

bool BPlusTree::erase(int64_t key) {
  if (!root_) return false;
  bool removed = false;
  erase_rec(root_, key, &removed);
  if (removed) count_--;
  // collapse the root if it went empty
  if (!root_->leaf && root_->keys.empty()) {
    Node* old = root_;
    root_ = root_->children[0];
    old->children.clear();
    delete old;
  } else if (root_->leaf && root_->keys.empty()) {
    delete root_;
    root_ = nullptr;
  }
  return removed;
}

void BPlusTree::erase_rec(Node* node, int64_t key, bool* removed) {
  if (node->leaf) {
    auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
    if (it != node->keys.end() && *it == key) {
      int pos = static_cast<int>(it - node->keys.begin());
      node->keys.erase(node->keys.begin() + pos);
      node->vals.erase(node->vals.begin() + pos);
      *removed = true;
    }
    return;
  }
  auto it = std::upper_bound(node->keys.begin(), node->keys.end(), key);
  int ci = static_cast<int>(it - node->keys.begin());
  Node* child = node->children[ci];
  erase_rec(child, key, removed);
  if (static_cast<int>(child->keys.size()) < MIN_KEYS) fix_child(node, ci);
}

void BPlusTree::fix_child(Node* parent, int ci) {
  Node* child = parent->children[ci];
  Node* left = ci > 0 ? parent->children[ci - 1] : nullptr;
  Node* right = ci + 1 < static_cast<int>(parent->children.size())
                    ? parent->children[ci + 1]
                    : nullptr;

  if (left && static_cast<int>(left->keys.size()) > MIN_KEYS) {
    // borrow the largest entry from the left sibling
    if (child->leaf) {
      child->keys.insert(child->keys.begin(), left->keys.back());
      child->vals.insert(child->vals.begin(), left->vals.back());
      left->keys.pop_back();
      left->vals.pop_back();
      parent->keys[ci - 1] = child->keys.front();
    } else {
      child->keys.insert(child->keys.begin(), parent->keys[ci - 1]);
      child->children.insert(child->children.begin(), left->children.back());
      parent->keys[ci - 1] = left->keys.back();
      left->keys.pop_back();
      left->children.pop_back();
    }
  } else if (right && static_cast<int>(right->keys.size()) > MIN_KEYS) {
    // borrow the smallest entry from the right sibling
    if (child->leaf) {
      child->keys.push_back(right->keys.front());
      child->vals.push_back(right->vals.front());
      right->keys.erase(right->keys.begin());
      right->vals.erase(right->vals.begin());
      parent->keys[ci] = right->keys.front();
    } else {
      child->keys.push_back(parent->keys[ci]);
      child->children.push_back(right->children.front());
      parent->keys[ci] = right->keys.front();
      right->keys.erase(right->keys.begin());
      right->children.erase(right->children.begin());
    }
  } else if (left) {
    // merge child into the left sibling
    if (child->leaf) {
      left->keys.insert(left->keys.end(), child->keys.begin(), child->keys.end());
      left->vals.insert(left->vals.end(), child->vals.begin(), child->vals.end());
      left->next = child->next;
    } else {
      left->keys.push_back(parent->keys[ci - 1]);
      left->keys.insert(left->keys.end(), child->keys.begin(), child->keys.end());
      left->children.insert(left->children.end(), child->children.begin(),
                            child->children.end());
    }
    parent->keys.erase(parent->keys.begin() + (ci - 1));
    parent->children.erase(parent->children.begin() + ci);
    child->children.clear();
    delete child;
  } else if (right) {
    // merge the right sibling into child
    if (child->leaf) {
      child->keys.insert(child->keys.end(), right->keys.begin(), right->keys.end());
      child->vals.insert(child->vals.end(), right->vals.begin(), right->vals.end());
      child->next = right->next;
    } else {
      child->keys.push_back(parent->keys[ci]);
      child->keys.insert(child->keys.end(), right->keys.begin(), right->keys.end());
      child->children.insert(child->children.end(), right->children.begin(),
                             right->children.end());
    }
    parent->keys.erase(parent->keys.begin() + ci);
    parent->children.erase(parent->children.begin() + (ci + 1));
    right->children.clear();
    delete right;
  }
}

}  // namespace minidb
