#include "storage/btree/bplus_tree.h"
#include <mutex>
#include <filesystem>
#include <iostream>

namespace minidb {

BPlusTree::BPlusTree(size_t order) : order_(order) {
    // Initialize a physical disk file to act as our storage medium
    std::string filename = "btree_pages.db";
    std::filesystem::remove(filename);
    
    // Create and truncate
    disk_file_.open(filename, std::ios::out | std::ios::binary | std::ios::trunc);
    disk_file_.close();
    
    // Reopen for Read/Write
    disk_file_.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    
    root_ = std::make_shared<BPlusNode>(true, next_page_id_++);
}

BPlusTree::~BPlusTree() {
    if (disk_file_.is_open()) {
        disk_file_.close();
    }
}

// SIMULATES THE BUFFER POOL EVICTION POLICY
void BPlusTree::MarkDirty(uint32_t page_id) {
    dirty_pages_.insert(page_id);
    
    // If our RAM is full, we must evict a dirty page to the physical disk.
    // This is the core bottleneck of B+ Trees (Random Disk I/O).
    if (dirty_pages_.size() >= MAX_DIRTY_PAGES) {
        auto it = dirty_pages_.begin();
        uint32_t evict_id = *it;
        dirty_pages_.erase(it);

        // Simulate flushing a 4KB page
        char buffer[4096] = {0}; 
        disk_file_.seekp(evict_id * 4096);
        disk_file_.write(buffer, 4096);
        disk_file_.flush(); // Block the thread until the disk acknowledges
    }
}

void BPlusTree::Insert(const std::string& key, const std::string& value) {
    std::unique_lock<std::shared_mutex> lock(rw_latch_);
    
    auto split_result = InsertInternal(root_, key, value);
    
    if (split_result.has_value()) {
        auto new_root = std::make_shared<BPlusNode>(false, next_page_id_++);
        new_root->keys.push_back(split_result->split_key);
        new_root->children.push_back(split_result->left);
        new_root->children.push_back(split_result->right);
        root_ = new_root;
        MarkDirty(root_->page_id);
    }
}

std::optional<BPlusTree::SplitResult> BPlusTree::InsertInternal(std::shared_ptr<BPlusNode> node, const std::string& key, const std::string& value) {
    // Every time we touch a node, it becomes dirty in the buffer pool
    MarkDirty(node->page_id); 

    if (node->is_leaf) {
        auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
        int index = std::distance(node->keys.begin(), it);

        if (it != node->keys.end() && *it == key) {
            node->values[index] = value;
            return std::nullopt;
        }

        node->keys.insert(it, key);
        node->values.insert(node->values.begin() + index, value);

        if (node->keys.size() >= order_) {
            auto new_leaf = std::make_shared<BPlusNode>(true, next_page_id_++);
            int mid = node->keys.size() / 2;

            new_leaf->keys.assign(node->keys.begin() + mid, node->keys.end());
            new_leaf->values.assign(node->values.begin() + mid, node->values.end());
            
            node->keys.resize(mid);
            node->values.resize(mid);

            new_leaf->next = node->next;
            node->next = new_leaf;

            MarkDirty(new_leaf->page_id);

            return SplitResult{new_leaf->keys[0], node, new_leaf};
        }
        return std::nullopt;
    }

    auto it = std::upper_bound(node->keys.begin(), node->keys.end(), key);
    int index = std::distance(node->keys.begin(), it);

    auto child_split = InsertInternal(node->children[index], key, value);

    if (child_split.has_value()) {
        node->keys.insert(node->keys.begin() + index, child_split->split_key);
        node->children.insert(node->children.begin() + index + 1, child_split->right);

        if (node->keys.size() >= order_) {
            auto new_internal = std::make_shared<BPlusNode>(false, next_page_id_++);
            int mid = node->keys.size() / 2;

            std::string mid_key = node->keys[mid];

            new_internal->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
            new_internal->children.assign(node->children.begin() + mid + 1, node->children.end());

            node->keys.resize(mid);
            node->children.resize(mid + 1);

            MarkDirty(new_internal->page_id);

            return SplitResult{mid_key, node, new_internal};
        }
    }
    return std::nullopt;
}

std::optional<std::string> BPlusTree::Search(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(rw_latch_);
    std::shared_ptr<BPlusNode> current = root_;

    while (!current->is_leaf) {
        auto it = std::upper_bound(current->keys.begin(), current->keys.end(), key);
        int index = std::distance(current->keys.begin(), it);
        current = current->children[index];
    }

    auto it = std::lower_bound(current->keys.begin(), current->keys.end(), key);
    if (it != current->keys.end() && *it == key) {
        int index = std::distance(current->keys.begin(), it);
        return current->values[index];
    }

    return std::nullopt;
}

void BPlusTree::Delete(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(rw_latch_);
    if (root_) {
        DeleteInternal(root_, key);
    }
}

bool BPlusTree::DeleteInternal(std::shared_ptr<BPlusNode> node, const std::string& key) {
    MarkDirty(node->page_id);

    // If we hit the leaf, find and erase the key
    if (node->is_leaf) {
        auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
        if (it != node->keys.end() && *it == key) {
            int index = std::distance(node->keys.begin(), it);
            node->keys.erase(node->keys.begin() + index);
            node->values.erase(node->values.begin() + index);
            return true;
        }
        return false; // Key not found
    }

    // Otherwise, route down the tree
    auto it = std::upper_bound(node->keys.begin(), node->keys.end(), key);
    int index = std::distance(node->keys.begin(), it);
    return DeleteInternal(node->children[index], key);
}

} // namespace minidb