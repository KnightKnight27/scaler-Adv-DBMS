#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <algorithm>
#include <shared_mutex>
#include <fstream>
#include <unordered_set>
#include <atomic>

namespace minidb {

// Simulates a 4KB disk page
struct BPlusNode {
    bool is_leaf;
    uint32_t page_id;
    std::vector<std::string> keys;
    std::vector<std::string> values;
    std::shared_ptr<BPlusNode> next;
    std::vector<std::shared_ptr<BPlusNode>> children;

    explicit BPlusNode(bool leaf, uint32_t pid) : is_leaf(leaf), page_id(pid) {}
};

class BPlusTree {
private:
    std::shared_ptr<BPlusNode> root_;
    size_t order_;
    mutable std::shared_mutex rw_latch_;
    
    // --- Buffer Pool & Disk Simulation ---
    std::atomic<uint32_t> next_page_id_{0};
    std::fstream disk_file_;
    
    // Tracks pages currently in RAM
    std::unordered_set<uint32_t> dirty_pages_;
    
    // Artificially small Buffer Pool to force disk thrashing under heavy load
    const size_t MAX_DIRTY_PAGES = 100; 
    
    void MarkDirty(uint32_t page_id);
    // -------------------------------------

    struct SplitResult {
        std::string split_key;
        std::shared_ptr<BPlusNode> left;
        std::shared_ptr<BPlusNode> right;
    };

    std::optional<SplitResult> InsertInternal(std::shared_ptr<BPlusNode> node, const std::string& key, const std::string& value);

public:
    explicit BPlusTree(size_t order = 50);
    ~BPlusTree();

    void Insert(const std::string& key, const std::string& value);
    std::optional<std::string> Search(const std::string& key) const;
};

} // namespace minidb