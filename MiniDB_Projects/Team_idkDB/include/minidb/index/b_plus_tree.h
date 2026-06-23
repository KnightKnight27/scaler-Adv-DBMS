#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "minidb/common/types.h"

namespace minidb {

class BPlusTree {
 public:
  explicit BPlusTree(std::size_t order = 8);

  std::optional<RID> Search(int key) const;
  bool Insert(int key, RID rid);
  bool Delete(int key);
  std::vector<std::pair<int, RID>> Entries() const;
  std::size_t Size() const { return size_; }
  std::size_t Height() const;
  bool Validate() const;

 private:
  struct Node {
    explicit Node(bool leaf_node) : leaf(leaf_node) {}
    bool leaf;
    std::vector<int> keys;
    std::vector<RID> values;
    std::vector<std::unique_ptr<Node>> children;
    Node *next{nullptr};
  };

  struct Split {
    int separator;
    std::unique_ptr<Node> right;
  };

  std::optional<Split> InsertRecursive(Node *node, int key, RID rid);
  void Rebuild(const std::vector<std::pair<int, RID>> &entries);
  const Node *FindLeaf(int key) const;
  Node *FindLeaf(int key);
  bool ValidateNode(const Node *node, int depth, int &leaf_depth,
                    std::optional<int> low,
                    std::optional<int> high) const;

  std::unique_ptr<Node> root_;
  std::size_t order_;
  std::size_t size_{0};
};

}  // namespace minidb
