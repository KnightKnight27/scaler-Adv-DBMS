#include "index/bplus_tree.h"

namespace minidb {

// In an internal node, return the index of the child to follow for `key`.
// child[i] covers keys < keys[i]; the last child covers keys >= keys.back().
static int childIndex(const vector<Value> &keys, const Value &key) {
  int i = 0;
  while (i < static_cast<int>(keys.size()) && !(key.compare(keys[i]) < 0)) ++i;
  return i;  // first i where key < keys[i]; or keys.size() if key >= all
}

// In a leaf, return the index of the first key >= `key`.
static int leafIndex(const vector<Value> &keys, const Value &key) {
  int i = 0;
  while (i < static_cast<int>(keys.size()) && keys[i].compare(key) < 0) ++i;
  return i;
}

void BPlusTree::insert(const Value &key, const RID &rid) {
  if (root_ == nullptr) {           // empty tree: create the first leaf
    root_ = new Node(true);
    root_->keys.push_back(key);
    root_->rids.push_back(rid);
    return;
  }
  InsertResult r = insertInto(root_, key, rid);
  if (r.split) {                    // the root split: grow the tree upward
    Node *new_root = new Node(false);
    new_root->keys.push_back(r.up_key);
    new_root->children.push_back(root_);
    new_root->children.push_back(r.right);
    root_ = new_root;
  }
}

BPlusTree::InsertResult BPlusTree::insertInto(Node *node, const Value &key,
                                              const RID &rid) {
  InsertResult result;  // default: no split

  if (node->is_leaf) {
    int i = leafIndex(node->keys, key);   // first slot with key >= new key
    node->keys.insert(node->keys.begin() + i, key);   // duplicates allowed
    node->rids.insert(node->rids.begin() + i, rid);

    if (static_cast<int>(node->keys.size()) <= order_) return result;  // fits

    // --- Split the overfull leaf ---
    int mid = static_cast<int>(node->keys.size()) / 2;
    Node *right = new Node(true);
    right->keys.assign(node->keys.begin() + mid, node->keys.end());
    right->rids.assign(node->rids.begin() + mid, node->rids.end());
    node->keys.resize(mid);
    node->rids.resize(mid);
    right->next = node->next;       // splice into the leaf chain
    node->next = right;
    result.split = true;
    result.up_key = right->keys.front();  // COPY first key of right up
    result.right = right;
    return result;
  }

  // Internal node: recurse into the correct child.
  int ci = childIndex(node->keys, key);
  InsertResult child = insertInto(node->children[ci], key, rid);
  if (!child.split) return result;

  // The child split: absorb its promoted key and new right sibling.
  node->keys.insert(node->keys.begin() + ci, child.up_key);
  node->children.insert(node->children.begin() + ci + 1, child.right);

  if (static_cast<int>(node->keys.size()) <= order_) return result;  // fits

  // --- Split the overfull internal node (the middle key MOVES up) ---
  int mid = static_cast<int>(node->keys.size()) / 2;
  Node *right = new Node(false);
  Value up = node->keys[mid];
  right->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
  right->children.assign(node->children.begin() + mid + 1, node->children.end());
  node->keys.resize(mid);
  node->children.resize(mid + 1);
  result.split = true;
  result.up_key = up;
  result.right = right;
  return result;
}

BPlusTree::Node *BPlusTree::findLeaf(const Value &key) const {
  Node *n = root_;
  while (n != nullptr && !n->is_leaf) n = n->children[childIndex(n->keys, key)];
  return n;
}

bool BPlusTree::search(const Value &key, RID *out) const {
  Node *leaf = findLeaf(key);
  if (leaf == nullptr) return false;
  int i = leafIndex(leaf->keys, key);
  if (i < static_cast<int>(leaf->keys.size()) && leaf->keys[i].compare(key) == 0) {
    *out = leaf->rids[i];
    return true;
  }
  return false;
}

void BPlusTree::remove(const Value &key, const RID &rid) {
  // Equal keys are contiguous and findLeaf lands on the leaf where the run
  // begins, so we scan forward across leaves until we pass `key`.
  Node *leaf = findLeaf(key);
  while (leaf != nullptr) {
    for (size_t i = 0; i < leaf->keys.size(); ++i) {
      int c = leaf->keys[i].compare(key);
      if (c > 0) return;                          // gone past the key: not present
      if (c == 0 && leaf->rids[i] == rid) {       // exact (key, rid) match
        leaf->keys.erase(leaf->keys.begin() + i);
        leaf->rids.erase(leaf->rids.begin() + i);
        return;
      }
    }
    leaf = leaf->next;                            // run may continue in next leaf
  }
}

vector<RID> BPlusTree::range(const Value *low, const Value *high) const {
  vector<RID> out;
  if (root_ == nullptr) return out;

  // Find the leaf where the scan starts.
  Node *leaf;
  if (low == nullptr) {                 // unbounded below: leftmost leaf
    leaf = root_;
    while (!leaf->is_leaf) leaf = leaf->children.front();
  } else {
    leaf = findLeaf(*low);
  }

  // Walk the leaf chain collecting in-range keys until we pass `high`.
  while (leaf != nullptr) {
    for (size_t i = 0; i < leaf->keys.size(); ++i) {
      const Value &k = leaf->keys[i];
      if (low != nullptr && k.compare(*low) < 0) continue;     // before range
      if (high != nullptr && k.compare(*high) > 0) return out; // past range: done
      out.push_back(leaf->rids[i]);
    }
    leaf = leaf->next;
  }
  return out;
}

void BPlusTree::destroy(Node *node) {
  if (node == nullptr) return;
  if (!node->is_leaf)
    for (Node *c : node->children) destroy(c);
  delete node;
}

}  // namespace minidb
