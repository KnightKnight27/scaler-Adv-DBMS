#include "minidb/index/b_plus_tree.h"

#include <algorithm>
#include <iterator>

namespace minidb {

BPlusTree::BPlusTree(std::size_t max_keys)
    : root_(std::make_unique<Node>(true)), max_keys_(std::max<std::size_t>(3, max_keys)) {}

bool BPlusTree::Empty() const { return size_ == 0; }

bool BPlusTree::Insert(std::string key, Rid rid) {
  bool existed = Search(key).has_value();
  auto split = InsertIntoNode(*root_, std::move(key), rid);
  if (split.has_value()) {
    auto new_root = std::make_unique<Node>(false);
    new_root->keys.push_back(std::move(split->separator));
    new_root->children.push_back(std::move(root_));
    new_root->children.push_back(std::move(split->right));
    root_ = std::move(new_root);
  }
  if (!existed) {
    ++size_;
  }
  return !existed;
}

std::optional<Rid> BPlusTree::Search(std::string_view key) const {
  const Node* leaf = FindLeaf(key);
  auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key,
                             [](const std::string& lhs, std::string_view rhs) { return lhs < rhs; });
  if (it == leaf->keys.end() || *it != key) {
    return std::nullopt;
  }
  return leaf->values[static_cast<std::size_t>(it - leaf->keys.begin())];
}

bool BPlusTree::Delete(std::string_view key) {
  Node* leaf = FindLeaf(key);
  auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key,
                             [](const std::string& lhs, std::string_view rhs) { return lhs < rhs; });
  if (it == leaf->keys.end() || *it != key) {
    return false;
  }
  auto index = static_cast<std::size_t>(it - leaf->keys.begin());
  leaf->keys.erase(leaf->keys.begin() + static_cast<std::ptrdiff_t>(index));
  leaf->values.erase(leaf->values.begin() + static_cast<std::ptrdiff_t>(index));
  --size_;
  return true;
}

std::vector<std::pair<std::string, Rid>> BPlusTree::Scan() const {
  std::vector<std::pair<std::string, Rid>> rows;
  for (const Node* leaf = FirstLeaf(); leaf != nullptr; leaf = leaf->next) {
    for (std::size_t i = 0; i < leaf->keys.size(); ++i) {
      rows.push_back({leaf->keys[i], leaf->values[i]});
    }
  }
  return rows;
}

std::optional<BPlusTree::SplitResult> BPlusTree::InsertIntoNode(Node& node, std::string key, Rid rid) {
  if (node.leaf) {
    auto it = std::lower_bound(node.keys.begin(), node.keys.end(), key);
    auto index = static_cast<std::size_t>(it - node.keys.begin());
    if (it != node.keys.end() && *it == key) {
      node.values[index] = rid;
      return std::nullopt;
    }
    node.keys.insert(it, std::move(key));
    node.values.insert(node.values.begin() + static_cast<std::ptrdiff_t>(index), rid);
    return SplitLeaf(node);
  }

  auto child_it = std::upper_bound(node.keys.begin(), node.keys.end(), key);
  auto child_index = static_cast<std::size_t>(child_it - node.keys.begin());
  auto split = InsertIntoNode(*node.children[child_index], std::move(key), rid);
  if (!split.has_value()) {
    return std::nullopt;
  }

  node.keys.insert(node.keys.begin() + static_cast<std::ptrdiff_t>(child_index),
                   std::move(split->separator));
  node.children.insert(node.children.begin() + static_cast<std::ptrdiff_t>(child_index + 1),
                       std::move(split->right));
  return SplitInternal(node);
}

std::optional<BPlusTree::SplitResult> BPlusTree::SplitLeaf(Node& node) {
  if (node.keys.size() <= max_keys_) {
    return std::nullopt;
  }

  auto right = std::make_unique<Node>(true);
  std::size_t mid = node.keys.size() / 2;
  right->keys.assign(std::make_move_iterator(node.keys.begin() + static_cast<std::ptrdiff_t>(mid)),
                     std::make_move_iterator(node.keys.end()));
  right->values.assign(node.values.begin() + static_cast<std::ptrdiff_t>(mid), node.values.end());
  node.keys.erase(node.keys.begin() + static_cast<std::ptrdiff_t>(mid), node.keys.end());
  node.values.erase(node.values.begin() + static_cast<std::ptrdiff_t>(mid), node.values.end());
  right->next = node.next;
  node.next = right.get();
  return SplitResult{right->keys.front(), std::move(right)};
}

std::optional<BPlusTree::SplitResult> BPlusTree::SplitInternal(Node& node) {
  if (node.keys.size() <= max_keys_) {
    return std::nullopt;
  }

  auto right = std::make_unique<Node>(false);
  std::size_t mid = node.keys.size() / 2;
  std::string separator = std::move(node.keys[mid]);

  right->keys.assign(std::make_move_iterator(node.keys.begin() + static_cast<std::ptrdiff_t>(mid + 1)),
                     std::make_move_iterator(node.keys.end()));
  node.keys.erase(node.keys.begin() + static_cast<std::ptrdiff_t>(mid), node.keys.end());

  for (auto it = node.children.begin() + static_cast<std::ptrdiff_t>(mid + 1); it != node.children.end();
       ++it) {
    right->children.push_back(std::move(*it));
  }
  node.children.erase(node.children.begin() + static_cast<std::ptrdiff_t>(mid + 1), node.children.end());
  return SplitResult{std::move(separator), std::move(right)};
}

BPlusTree::Node* BPlusTree::FindLeaf(std::string_view key) {
  Node* node = root_.get();
  while (!node->leaf) {
    auto it = std::upper_bound(key, node->keys.begin(), node->keys.end(),
                               [](std::string_view lhs, const std::string& rhs) { return lhs < rhs; });
    node = node->children[static_cast<std::size_t>(it - node->keys.begin())].get();
  }
  return node;
}

const BPlusTree::Node* BPlusTree::FindLeaf(std::string_view key) const {
  const Node* node = root_.get();
  while (!node->leaf) {
    auto it = std::upper_bound(key, node->keys.begin(), node->keys.end(),
                               [](std::string_view lhs, const std::string& rhs) { return lhs < rhs; });
    node = node->children[static_cast<std::size_t>(it - node->keys.begin())].get();
  }
  return node;
}

const BPlusTree::Node* BPlusTree::FirstLeaf() const {
  const Node* node = root_.get();
  while (!node->leaf) {
    node = node->children.front().get();
  }
  return node;
}

}  // namespace minidb
