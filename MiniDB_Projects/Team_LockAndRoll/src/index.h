#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "common.h"

namespace minidb {

class BPlusTree {
 public:
  static constexpr int ORDER = 64;
  static constexpr int MAX_KEYS = ORDER - 1;
  static constexpr int MIN_KEYS = (ORDER + 1) / 2 - 1;  // min keys in a non-root node

  BPlusTree() = default;
  ~BPlusTree() { destroy(root_); }

  BPlusTree(const BPlusTree&) = delete;
  BPlusTree& operator=(const BPlusTree&) = delete;

  // false if the key already exists, no overwrite
  bool insert(int64_t key, RID rid);
  std::optional<RID> search(int64_t key) const;
  bool erase(int64_t key);
  void range_scan(int64_t lo, int64_t hi, const std::function<void(int64_t, RID)>& fn) const;

  size_t size() const { return count_; }
  int height() const;

 private:
  struct Node {
    bool leaf;
    std::vector<int64_t> keys;
    std::vector<Node*> children;  // internal only; size == keys+1
    std::vector<RID> vals;        // leaf only; size == keys
    Node* next = nullptr;         // leaf chain
    explicit Node(bool is_leaf) : leaf(is_leaf) {}
  };

  struct SplitInfo {
    bool split = false;
    int64_t key = 0;
    Node* right = nullptr;
  };

  SplitInfo insert_rec(Node* node, int64_t key, RID rid, bool* inserted);
  void erase_rec(Node* node, int64_t key, bool* removed);
  void fix_child(Node* parent, int ci);
  Node* find_leaf(int64_t key) const;
  static void destroy(Node* n);

  Node* root_ = nullptr;
  size_t count_ = 0;
};

}  // namespace minidb
