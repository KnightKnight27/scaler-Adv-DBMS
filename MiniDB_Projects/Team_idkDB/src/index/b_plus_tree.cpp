#include "minidb/index/b_plus_tree.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#include "minidb/common/trace.h"

namespace minidb {

BPlusTree::BPlusTree(std::size_t order) : order_(order) {
  if (order < 3) throw std::invalid_argument("B+ tree order must be >= 3");
  root_ = std::make_unique<Node>(true);
}

const BPlusTree::Node *BPlusTree::FindLeaf(int key) const {
  const Node *node = root_.get();
  while (!node->leaf) {
    const auto it = std::upper_bound(node->keys.begin(), node->keys.end(), key);
    node = node->children[static_cast<std::size_t>(it - node->keys.begin())].get();
  }
  return node;
}

BPlusTree::Node *BPlusTree::FindLeaf(int key) {
  return const_cast<Node *>(std::as_const(*this).FindLeaf(key));
}

std::optional<RID> BPlusTree::Search(int key) const {
  const Node *leaf = FindLeaf(key);
  const auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
  Trace::Log("BTREE", "search key " + std::to_string(key));
  if (it == leaf->keys.end() || *it != key) return std::nullopt;
  return leaf->values[static_cast<std::size_t>(it - leaf->keys.begin())];
}

bool BPlusTree::Insert(int key, RID rid) {
  if (Search(key)) return false;
  auto split = InsertRecursive(root_.get(), key, rid);
  if (split) {
    auto new_root = std::make_unique<Node>(false);
    new_root->keys.push_back(split->separator);
    new_root->children.push_back(std::move(root_));
    new_root->children.push_back(std::move(split->right));
    root_ = std::move(new_root);
  }
  ++size_;
  Trace::Log("BTREE", "insert key " + std::to_string(key) + " -> page " +
                           std::to_string(rid.page_id));
  return true;
}

std::optional<BPlusTree::Split> BPlusTree::InsertRecursive(Node *node, int key,
                                                           RID rid) {
  if (node->leaf) {
    auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
    const auto position = static_cast<std::size_t>(it - node->keys.begin());
    node->keys.insert(it, key);
    node->values.insert(node->values.begin() + static_cast<std::ptrdiff_t>(position),
                        rid);
    if (node->keys.size() < order_) return std::nullopt;
    const std::size_t middle = node->keys.size() / 2;
    auto right = std::make_unique<Node>(true);
    right->keys.assign(node->keys.begin() + static_cast<std::ptrdiff_t>(middle),
                       node->keys.end());
    right->values.assign(
        node->values.begin() + static_cast<std::ptrdiff_t>(middle),
        node->values.end());
    node->keys.resize(middle);
    node->values.resize(middle);
    right->next = node->next;
    node->next = right.get();
    return Split{right->keys.front(), std::move(right)};
  }

  auto it = std::upper_bound(node->keys.begin(), node->keys.end(), key);
  const auto child_index =
      static_cast<std::size_t>(it - node->keys.begin());
  auto split = InsertRecursive(node->children[child_index].get(), key, rid);
  if (!split) return std::nullopt;
  node->keys.insert(node->keys.begin() + static_cast<std::ptrdiff_t>(child_index),
                    split->separator);
  node->children.insert(
      node->children.begin() + static_cast<std::ptrdiff_t>(child_index + 1),
      std::move(split->right));
  if (node->keys.size() < order_) return std::nullopt;

  const std::size_t middle = node->keys.size() / 2;
  const int separator = node->keys[middle];
  auto right = std::make_unique<Node>(false);
  right->keys.assign(
      node->keys.begin() + static_cast<std::ptrdiff_t>(middle + 1),
      node->keys.end());
  for (std::size_t i = middle + 1; i < node->children.size(); ++i) {
    right->children.push_back(std::move(node->children[i]));
  }
  node->keys.resize(middle);
  node->children.resize(middle + 1);
  return Split{separator, std::move(right)};
}

bool BPlusTree::Delete(int key) {
  if (!Search(key)) return false;
  auto entries = Entries();
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [key](const auto &entry) {
                                 return entry.first == key;
                               }),
                entries.end());
  Rebuild(entries);
  Trace::Log("BTREE", "delete key " + std::to_string(key));
  return true;
}

void BPlusTree::Rebuild(
    const std::vector<std::pair<int, RID>> &entries) {
  root_ = std::make_unique<Node>(true);
  size_ = 0;
  for (const auto &[key, rid] : entries) Insert(key, rid);
}

std::vector<std::pair<int, RID>> BPlusTree::Entries() const {
  const Node *node = root_.get();
  while (!node->leaf) node = node->children.front().get();
  std::vector<std::pair<int, RID>> entries;
  while (node) {
    for (std::size_t i = 0; i < node->keys.size(); ++i) {
      entries.emplace_back(node->keys[i], node->values[i]);
    }
    node = node->next;
  }
  return entries;
}

std::size_t BPlusTree::Height() const {
  std::size_t height = 1;
  const Node *node = root_.get();
  while (!node->leaf) {
    ++height;
    node = node->children.front().get();
  }
  return height;
}

bool BPlusTree::Validate() const {
  int leaf_depth = -1;
  if (!ValidateNode(root_.get(), 0, leaf_depth, std::nullopt, std::nullopt)) {
    return false;
  }
  const auto entries = Entries();
  return entries.size() == size_ &&
         std::is_sorted(entries.begin(), entries.end(),
                        [](const auto &a, const auto &b) {
                          return a.first < b.first;
                        });
}

bool BPlusTree::ValidateNode(const Node *node, int depth, int &leaf_depth,
                             std::optional<int> low,
                             std::optional<int> high) const {
  if (!std::is_sorted(node->keys.begin(), node->keys.end())) return false;
  for (int key : node->keys) {
    if (low && key < *low) return false;
    if (high && key >= *high) return false;
  }
  if (node->leaf) {
    if (node->keys.size() != node->values.size()) return false;
    if (leaf_depth == -1) leaf_depth = depth;
    return leaf_depth == depth;
  }
  if (node->children.size() != node->keys.size() + 1) return false;
  for (std::size_t i = 0; i < node->children.size(); ++i) {
    auto child_low = i == 0 ? low : std::optional<int>(node->keys[i - 1]);
    auto child_high =
        i == node->keys.size() ? high : std::optional<int>(node->keys[i]);
    if (!ValidateNode(node->children[i].get(), depth + 1, leaf_depth,
                      child_low, child_high)) {
      return false;
    }
  }
  return true;
}

}  // namespace minidb
