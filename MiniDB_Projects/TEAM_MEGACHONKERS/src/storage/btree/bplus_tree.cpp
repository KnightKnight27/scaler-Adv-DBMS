#include "storage/btree/bplus_tree.h"

namespace minidb {

void BPlusTree::Insert(const std::string& key, const std::string& value) {
    std::unique_lock<std::shared_mutex> lock(rw_latch_);
    
    auto split_result = InsertInternal(root_, key, value);
    
    // If the root itself split, we must create a new root
    if (split_result.has_value()) {
        auto new_root = std::make_shared<BPlusNode>(false);
        new_root->keys.push_back(split_result->split_key);
        new_root->children.push_back(split_result->left);
        new_root->children.push_back(split_result->right);
        root_ = new_root;
    }
}

std::optional<BPlusTree::SplitResult> BPlusTree::InsertInternal(std::shared_ptr<BPlusNode> node, const std::string& key, const std::string& value) {
    if (node->is_leaf) {
        // 1. Find insertion point
        auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
        int index = std::distance(node->keys.begin(), it);

        // Update in place if key already exists
        if (it != node->keys.end() && *it == key) {
            node->values[index] = value;
            return std::nullopt;
        }

        // Insert new key and value
        node->keys.insert(it, key);
        node->values.insert(node->values.begin() + index, value);

        // 2. Check for overflow and split leaf
        if (node->keys.size() >= order_) {
            auto new_leaf = std::make_shared<BPlusNode>(true);
            int mid = node->keys.size() / 2;

            // Move right half to new node
            new_leaf->keys.assign(node->keys.begin() + mid, node->keys.end());
            new_leaf->values.assign(node->values.begin() + mid, node->values.end());
            
            // Resize old node
            node->keys.resize(mid);
            node->values.resize(mid);

            // Update sibling pointers
            new_leaf->next = node->next;
            node->next = new_leaf;

            // Leaf nodes copy the middle key up to the parent
            return SplitResult{new_leaf->keys[0], node, new_leaf};
        }
        return std::nullopt;
    }

    // --- Internal Node Logic ---
    auto it = std::upper_bound(node->keys.begin(), node->keys.end(), key);
    int index = std::distance(node->keys.begin(), it);

    auto child_split = InsertInternal(node->children[index], key, value);

    if (child_split.has_value()) {
        // A child split, so we must insert the new split_key into this internal node
        node->keys.insert(node->keys.begin() + index, child_split->split_key);
        node->children.insert(node->children.begin() + index + 1, child_split->right);

        // Check for internal node overflow
        if (node->keys.size() >= order_) {
            auto new_internal = std::make_shared<BPlusNode>(false);
            int mid = node->keys.size() / 2;

            // Internal nodes *move* the middle key up, they don't keep a copy
            std::string mid_key = node->keys[mid];

            new_internal->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
            new_internal->children.assign(node->children.begin() + mid + 1, node->children.end());

            node->keys.resize(mid);
            node->children.resize(mid + 1);

            return SplitResult{mid_key, node, new_internal};
        }
    }
    return std::nullopt;
}

std::optional<std::string> BPlusTree::Search(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(rw_latch_);
    std::shared_ptr<BPlusNode> current = root_;

    // Traverse down to the leaf
    while (!current->is_leaf) {
        auto it = std::upper_bound(current->keys.begin(), current->keys.end(), key);
        int index = std::distance(current->keys.begin(), it);
        current = current->children[index];
    }

    // Binary search inside the leaf
    auto it = std::lower_bound(current->keys.begin(), current->keys.end(), key);
    if (it != current->keys.end() && *it == key) {
        int index = std::distance(current->keys.begin(), it);
        return current->values[index];
    }

    return std::nullopt;
}

} // namespace minidb