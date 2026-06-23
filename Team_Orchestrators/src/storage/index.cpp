#include "minidb/storage/index.hpp"

#include <utility>

namespace minidb {

// A node holds either child pointers (internal) or RID lists (leaf). Separator
// keys use copy-up semantics: an internal key equals the minimum key of the
// subtree to its right, so child c[i] covers [keys[i-1], keys[i]).
struct BPlusNode {
  bool is_leaf;
  std::vector<Value> keys;
  std::vector<std::unique_ptr<BPlusNode>> children;  // internal: size == keys+1
  std::vector<std::vector<RID>> rids;                // leaf: size == keys
  BPlusNode* next = nullptr;                          // leaf-chain (non-owning)
  explicit BPlusNode(bool leaf) : is_leaf(leaf) {}
};

namespace {

constexpr size_t kOrderConst = 64;

// First child index to descend for `key`: first i where key < keys[i]
// (upper_bound). With copy-up separators, key == keys[i] descends to c[i+1].
size_t child_index(const BPlusNode& n, const Value& key) {
  size_t lo = 0, hi = n.keys.size();
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (key < n.keys[mid]) hi = mid; else lo = mid + 1;
  }
  return lo;
}

// First position in a leaf where keys[i] >= key (lower_bound).
size_t leaf_pos(const BPlusNode& n, const Value& key) {
  size_t lo = 0, hi = n.keys.size();
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (n.keys[mid] < key) lo = mid + 1; else hi = mid;
  }
  return lo;
}

struct SplitResult {
  bool split = false;
  Value sep;                          // separator key promoted to the parent
  std::unique_ptr<BPlusNode> right;   // new right sibling
};

SplitResult insert_rec(BPlusNode* n, const Value& key, const RID& rid) {
  if (n->is_leaf) {
    size_t pos = leaf_pos(*n, key);
    if (pos < n->keys.size() && n->keys[pos] == key) {
      n->rids[pos].push_back(rid);
    } else {
      n->keys.insert(n->keys.begin() + pos, key);
      n->rids.insert(n->rids.begin() + pos, std::vector<RID>{rid});
    }
    if (n->keys.size() <= kOrderConst) return {};
    // Split leaf: right half moves to a new leaf; its first key copies up.
    size_t total = n->keys.size();
    size_t start = total / 2;
    auto right = std::unique_ptr<BPlusNode>(new BPlusNode(true));
    for (size_t i = start; i < total; ++i) {
      right->keys.push_back(std::move(n->keys[i]));
      right->rids.push_back(std::move(n->rids[i]));
    }
    n->keys.resize(start);
    n->rids.resize(start);
    right->next = n->next;
    n->next = right.get();
    SplitResult s;
    s.split = true;
    s.sep = right->keys.front();
    s.right = std::move(right);
    return s;
  }

  // Internal: descend, then absorb any child split.
  size_t ci = child_index(*n, key);
  SplitResult cs = insert_rec(n->children[ci].get(), key, rid);
  if (!cs.split) return {};
  n->keys.insert(n->keys.begin() + ci, std::move(cs.sep));
  n->children.insert(n->children.begin() + ci + 1, std::move(cs.right));
  if (n->keys.size() <= kOrderConst) return {};
  // Split internal: median key is promoted (moved out, not copied).
  size_t total = n->keys.size();
  size_t mid = total / 2;
  Value up = std::move(n->keys[mid]);
  auto right = std::unique_ptr<BPlusNode>(new BPlusNode(false));
  for (size_t i = mid + 1; i < total; ++i) right->keys.push_back(std::move(n->keys[i]));
  for (size_t i = mid + 1; i < n->children.size(); ++i)
    right->children.push_back(std::move(n->children[i]));
  n->keys.resize(mid);
  n->children.resize(mid + 1);
  SplitResult s;
  s.split = true;
  s.sep = std::move(up);
  s.right = std::move(right);
  return s;
}

const BPlusNode* descend_to_leaf(const BPlusNode* n, const Value& key) {
  while (n && !n->is_leaf) n = n->children[child_index(*n, key)].get();
  return n;
}

}  // namespace

BPlusTree::BPlusTree() = default;
BPlusTree::~BPlusTree() = default;
BPlusTree::BPlusTree(BPlusTree&&) noexcept = default;
BPlusTree& BPlusTree::operator=(BPlusTree&&) noexcept = default;

void BPlusTree::insert(const Value& key, const RID& rid) {
  if (!root_) root_ = std::unique_ptr<BPlusNode>(new BPlusNode(true));
  SplitResult s = insert_rec(root_.get(), key, rid);
  if (s.split) {
    auto new_root = std::unique_ptr<BPlusNode>(new BPlusNode(false));
    new_root->keys.push_back(std::move(s.sep));
    new_root->children.push_back(std::move(root_));
    new_root->children.push_back(std::move(s.right));
    root_ = std::move(new_root);
  }
  ++count_;
}

std::vector<RID> BPlusTree::find(const Value& key) const {
  const BPlusNode* leaf = descend_to_leaf(root_.get(), key);
  if (!leaf) return {};
  size_t pos = leaf_pos(*leaf, key);
  if (pos < leaf->keys.size() && leaf->keys[pos] == key) return leaf->rids[pos];
  return {};
}

std::vector<RID> BPlusTree::range(const Value& lo, bool lo_incl, const Value& hi,
                                  bool hi_incl) const {
  std::vector<RID> out;
  const BPlusNode* leaf = descend_to_leaf(root_.get(), lo);
  while (leaf) {
    for (size_t i = 0; i < leaf->keys.size(); ++i) {
      const Value& k = leaf->keys[i];
      if (k < lo || (!lo_incl && k == lo)) continue;
      if (hi < k || (!hi_incl && k == hi)) return out;  // past the upper bound
      for (const RID& r : leaf->rids[i]) out.push_back(r);
    }
    leaf = leaf->next;
  }
  return out;
}

bool BPlusTree::remove(const Value& key, const RID& rid) {
  // Re-descend mutably from the root.
  BPlusNode* n = root_.get();
  while (n && !n->is_leaf) n = n->children[child_index(*n, key)].get();
  if (!n) return false;
  size_t pos = leaf_pos(*n, key);
  if (pos >= n->keys.size() || !(n->keys[pos] == key)) return false;
  std::vector<RID>& v = n->rids[pos];
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] == rid) {
      v.erase(v.begin() + i);
      --count_;
      if (v.empty()) {
        n->keys.erase(n->keys.begin() + pos);
        n->rids.erase(n->rids.begin() + pos);
      }
      return true;
    }
  }
  return false;
}

}  // namespace minidb
