#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <algorithm>
#include <shared_mutex>

namespace minidb {

struct BPlusNode {
    bool is_leaf;
    std::vector<std::string> keys;
    
    // For Leaf Nodes: Holds the actual data
    std::vector<std::string> values;
    std::shared_ptr<BPlusNode> next; // Sibling pointer for fast range scans
    
    // For Internal Nodes: Holds pointers to children
    std::vector<std::shared_ptr<BPlusNode>> children;

    explicit BPlusNode(bool leaf) : is_leaf(leaf) {}
};

class BPlusTree {
private:
    std::shared_ptr<BPlusNode> root_;
    size_t order_; // Maximum number of children per node
    mutable std::shared_mutex rw_latch_; // Thread safety

    // Internal recursive structures
    struct SplitResult {
        std::string split_key;
        std::shared_ptr<BPlusNode> left;
        std::shared_ptr<BPlusNode> right;
    };

    std::optional<SplitResult> InsertInternal(std::shared_ptr<BPlusNode> node, const std::string& key, const std::string& value);

public:
    // Order of 100 is a standard starting point for database page sizes
    explicit BPlusTree(size_t order = 100) : root_(std::make_shared<BPlusNode>(true)), order_(order) {}

    void Insert(const std::string& key, const std::string& value);
    std::optional<std::string> Search(const std::string& key) const;
};

} // namespace minidb