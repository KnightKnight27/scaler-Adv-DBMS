#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "minidb/common/types.h"

namespace minidb {

class BPlusTree {
 public:
  explicit BPlusTree(std::size_t max_keys = 4);

  bool Empty() const;
  std::size_t Size() const { return size_; }
  bool Insert(std::string key, Rid rid);
  std::optional<Rid> Search(std::string_view key) const;
  bool Delete(std::string_view key);
  std::vector<std::pair<std::string, Rid>> Scan() const;

 private:
  struct Node {
    explicit Node(bool is_leaf) : leaf(is_leaf) {}

    bool leaf;
    std::vector<std::string> keys;
    std::vector<Rid> values;
    std::vector<std::unique_ptr<Node>> children;
    Node* next{nullptr};
  };

  struct SplitResult {
    std::string separator;
    std::unique_ptr<Node> right;
  };

  std::optional<SplitResult> InsertIntoNode(Node& node, std::string key, Rid rid);
  std::optional<SplitResult> SplitLeaf(Node& node);
  std::optional<SplitResult> SplitInternal(Node& node);
  Node* FindLeaf(std::string_view key);
  const Node* FindLeaf(std::string_view key) const;
  const Node* FirstLeaf() const;

  std::unique_ptr<Node> root_;
  std::size_t max_keys_;
  std::size_t size_{0};
};

}  // namespace minidb
