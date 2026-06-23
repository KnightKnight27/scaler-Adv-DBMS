#include "index/bplus_tree.h"
#include <algorithm>
#include <cassert>

namespace minidb {

BPlusTree::BPlusTree(int order) : order_(order) {
    assert(order >= 3); // minimum viable order
}

BPlusTree::~BPlusTree() {
    if (root_) DestroyTree(root_);
}

void BPlusTree::DestroyTree(Node* node) {
    if (!node) return;
    if (!node->is_leaf) {
        auto* internal = static_cast<InternalNode*>(node);
        for (auto* child : internal->children) {
            DestroyTree(child);
        }
    }
    delete node;
}

// ── Search ──────────────────────────────────────────────────

BPlusTree::LeafNode* BPlusTree::FindLeaf(int64_t key) const {
    if (!root_) return nullptr;

    Node* cur = root_;
    while (!cur->is_leaf) {
        auto* internal = static_cast<InternalNode*>(cur);
        // Find child to follow
        size_t i = 0;
        while (i < internal->keys.size() && key >= internal->keys[i]) {
            i++;
        }
        cur = internal->children[i];
    }
    return static_cast<LeafNode*>(cur);
}

std::optional<RecordId> BPlusTree::Search(int64_t key) const {
    LeafNode* leaf = FindLeaf(key);
    if (!leaf) return std::nullopt;

    for (size_t i = 0; i < leaf->keys.size(); i++) {
        if (leaf->keys[i] == key) {
            return leaf->values[i];
        }
    }
    return std::nullopt;
}

std::vector<RecordId> BPlusTree::RangeSearch(int64_t lo, int64_t hi) const {
    std::vector<RecordId> result;
    LeafNode* leaf = FindLeaf(lo);
    if (!leaf) return result;

    // Skip to first key >= lo
    while (leaf) {
        for (size_t i = 0; i < leaf->keys.size(); i++) {
            if (leaf->keys[i] >= lo && leaf->keys[i] <= hi) {
                result.push_back(leaf->values[i]);
            }
            if (leaf->keys[i] > hi) {
                return result;
            }
        }
        leaf = leaf->next;
    }
    return result;
}

// ── Insert ──────────────────────────────────────────────────

void BPlusTree::Insert(int64_t key, const RecordId& rid) {
    if (!root_) {
        // First insert — create root leaf
        auto* leaf = new LeafNode();
        leaf->keys.push_back(key);
        leaf->values.push_back(rid);
        root_ = leaf;
        size_++;
        return;
    }

    LeafNode* leaf = FindLeaf(key);

    // Check for duplicate key — update if exists
    for (size_t i = 0; i < leaf->keys.size(); i++) {
        if (leaf->keys[i] == key) {
            leaf->values[i] = rid; // update
            return;
        }
    }

    InsertIntoLeaf(leaf, key, rid);
    size_++;
}

void BPlusTree::InsertIntoLeaf(LeafNode* leaf, int64_t key, const RecordId& rid) {
    // Find insertion position
    auto pos = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    size_t idx = pos - leaf->keys.begin();
    leaf->keys.insert(pos, key);
    leaf->values.insert(leaf->values.begin() + idx, rid);

    // Check if we need to split
    if (static_cast<int>(leaf->keys.size()) > order_) {
        int64_t push_key;
        LeafNode* new_leaf;
        SplitLeaf(leaf, push_key, new_leaf);
        InsertIntoParent(leaf, push_key, new_leaf);
    }
}

void BPlusTree::SplitLeaf(LeafNode* leaf, int64_t& push_key, LeafNode*& new_leaf) {
    new_leaf = new LeafNode();
    int mid = (static_cast<int>(leaf->keys.size()) + 1) / 2;

    // Move second half to new leaf
    new_leaf->keys.assign(leaf->keys.begin() + mid, leaf->keys.end());
    new_leaf->values.assign(leaf->values.begin() + mid, leaf->values.end());
    leaf->keys.resize(mid);
    leaf->values.resize(mid);

    // Link siblings
    new_leaf->next = leaf->next;
    leaf->next = new_leaf;

    // The key to push up is the first key of the new leaf
    push_key = new_leaf->keys[0];
}

void BPlusTree::InsertIntoParent(Node* left, int64_t key, Node* right) {
    if (left == root_) {
        // Create new root
        auto* new_root = new InternalNode();
        new_root->keys.push_back(key);
        new_root->children.push_back(left);
        new_root->children.push_back(right);
        root_ = new_root;
        return;
    }

    InternalNode* parent = FindParent(left);
    assert(parent != nullptr);

    // Find position in parent
    size_t i = 0;
    while (i < parent->children.size() && parent->children[i] != left) i++;
    // Insert key and new child
    parent->keys.insert(parent->keys.begin() + i, key);
    parent->children.insert(parent->children.begin() + i + 1, right);

    // Check if parent needs splitting
    if (static_cast<int>(parent->keys.size()) > order_) {
        int64_t push_key2;
        InternalNode* new_internal;
        SplitInternal(parent, push_key2, new_internal);
        InsertIntoParent(parent, push_key2, new_internal);
    }
}

void BPlusTree::SplitInternal(InternalNode* node, int64_t& push_key, InternalNode*& new_node) {
    new_node = new InternalNode();
    int mid = static_cast<int>(node->keys.size()) / 2;

    push_key = node->keys[mid];

    // Move keys after mid to new node
    new_node->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
    new_node->children.assign(node->children.begin() + mid + 1, node->children.end());

    node->keys.resize(mid);
    node->children.resize(mid + 1);
}

BPlusTree::InternalNode* BPlusTree::FindParent(Node* target) const {
    if (root_ == target || !root_ || root_->is_leaf) return nullptr;
    return FindParentHelper(static_cast<InternalNode*>(root_), target);
}

BPlusTree::InternalNode* BPlusTree::FindParentHelper(InternalNode* current, Node* target) const {
    for (auto* child : current->children) {
        if (child == target) return current;
        if (!child->is_leaf) {
            auto* result = FindParentHelper(static_cast<InternalNode*>(child), target);
            if (result) return result;
        }
    }
    return nullptr;
}

// ── Delete ──────────────────────────────────────────────────

bool BPlusTree::Delete(int64_t key) {
    LeafNode* leaf = FindLeaf(key);
    if (!leaf) return false;

    bool deleted = DeleteFromLeaf(leaf, key);
    if (deleted) {
        size_--;
        // Simple approach: don't rebalance. Just remove from leaf.
        // If leaf becomes empty, we leave it (lazy deletion).
        // This is acceptable for a minimalist implementation.
        if (root_->is_leaf && static_cast<LeafNode*>(root_)->keys.empty()) {
            delete root_;
            root_ = nullptr;
        }
    }
    return deleted;
}

bool BPlusTree::DeleteFromLeaf(LeafNode* leaf, int64_t key) {
    for (size_t i = 0; i < leaf->keys.size(); i++) {
        if (leaf->keys[i] == key) {
            leaf->keys.erase(leaf->keys.begin() + i);
            leaf->values.erase(leaf->values.begin() + i);
            return true;
        }
    }
    return false;
}

} // namespace minidb
