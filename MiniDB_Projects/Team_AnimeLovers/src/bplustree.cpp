#include "bplustree.h"
#include <algorithm>
#include <cassert>
#include <stdexcept>

// ─── Helpers ──────────────────────────────────────────────────────────────────

// Index of the first key in `keys` that is >= target (lower-bound position)
static int lower_bound(const std::vector<Value>& keys, const Value& target) {
    int lo = 0, hi = static_cast<int>(keys.size());
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (keys[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

// ─── Public API ───────────────────────────────────────────────────────────────

std::optional<RID> BPlusTree::search(const Value& key) const {
    if (!root_) return std::nullopt;
    BPTNode* leaf = find_leaf(key);
    int i = lower_bound(leaf->keys, key);
    if (i < static_cast<int>(leaf->keys.size()) && leaf->keys[i] == key)
        return leaf->rids[i];
    return std::nullopt;
}

bool BPlusTree::insert(const Value& key, RID rid) {
    if (!root_) {
        root_ = new BPTNode();
        root_->is_leaf = true;
    }
    auto split = insert_recursive(root_, key, rid);
    if (split) {
        // Root was split — create a new root above it
        BPTNode* new_root = new BPTNode();
        new_root->is_leaf = false;
        new_root->keys.push_back(std::move(split->push_up_key));
        new_root->children.push_back(root_);
        new_root->children.push_back(split->right);
        root_ = new_root;
    }
    return true; // duplicate check done inside recursive call
}

bool BPlusTree::remove(const Value& key) {
    if (!root_) return false;
    BPTNode* leaf = find_leaf(key);
    return remove_from_leaf(leaf, key);
    // Note: we do not rebalance after deletion for simplicity. The tree may
    // become slightly unbalanced, but correctness is maintained — scans and
    // searches still work. A production implementation would merge or
    // redistribute nodes when occupancy falls below 50%.
}

std::vector<RID> BPlusTree::range_scan(const Value& lo, const Value& hi) const {
    std::vector<RID> result;
    if (!root_) return result;
    BPTNode* leaf = find_leaf(lo);
    // Walk the leaf linked list collecting all matching entries
    while (leaf) {
        for (int i = 0; i < static_cast<int>(leaf->keys.size()); ++i) {
            if (leaf->keys[i] > hi) return result; // past upper bound
            if (leaf->keys[i] >= lo)
                result.push_back(leaf->rids[i]);
        }
        leaf = leaf->next;
    }
    return result;
}

std::vector<std::pair<Value, RID>> BPlusTree::scan_all() const {
    std::vector<std::pair<Value, RID>> result;
    if (!root_) return result;
    // Descend to the leftmost leaf
    BPTNode* cur = root_;
    while (!cur->is_leaf) cur = cur->children.front();
    while (cur) {
        for (int i = 0; i < static_cast<int>(cur->keys.size()); ++i)
            result.emplace_back(cur->keys[i], cur->rids[i]);
        cur = cur->next;
    }
    return result;
}

// ─── Private helpers ──────────────────────────────────────────────────────────

BPTNode* BPlusTree::find_leaf(const Value& key) const {
    BPTNode* cur = root_;
    while (!cur->is_leaf) {
        int i = lower_bound(cur->keys, key);
        // i is the index of the first key >= target.
        // Children: child[0] < key[0] <= child[1] < key[1] ...
        // So if i == num_keys, go to the last child.
        if (i < static_cast<int>(cur->keys.size()) && cur->keys[i] == key)
            i++; // equal: go right child (>=)
        cur = cur->children[i];
    }
    return cur;
}

BPlusTree::SplitResult BPlusTree::split_leaf(BPTNode* leaf) {
    int mid = static_cast<int>(leaf->keys.size()) / 2;
    BPTNode* right = new BPTNode();
    right->is_leaf = true;

    // Move upper half to right sibling
    right->keys.assign(leaf->keys.begin() + mid, leaf->keys.end());
    right->rids.assign(leaf->rids.begin() + mid, leaf->rids.end());
    leaf->keys.erase(leaf->keys.begin() + mid, leaf->keys.end());
    leaf->rids.erase(leaf->rids.begin() + mid, leaf->rids.end());

    // Wire up the leaf linked list
    right->next = leaf->next;
    leaf->next  = right;

    // The smallest key in right sibling is pushed up to the parent
    return {right, right->keys.front()};
}

BPlusTree::SplitResult BPlusTree::split_internal(BPTNode* node) {
    int mid = static_cast<int>(node->keys.size()) / 2;
    Value push_up = node->keys[mid];

    BPTNode* right = new BPTNode();
    right->is_leaf = false;
    right->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
    right->children.assign(node->children.begin() + mid + 1, node->children.end());

    node->keys.erase(node->keys.begin() + mid, node->keys.end());
    node->children.erase(node->children.begin() + mid + 1, node->children.end());

    return {right, std::move(push_up)};
}

std::optional<BPlusTree::SplitResult>
BPlusTree::insert_recursive(BPTNode* node, const Value& key, RID rid) {
    if (node->is_leaf) {
        // Check for duplicate key (primary key constraint)
        int i = lower_bound(node->keys, key);
        if (i < static_cast<int>(node->keys.size()) && node->keys[i] == key)
            throw std::runtime_error("Duplicate primary key");

        // Insert in sorted position
        node->keys.insert(node->keys.begin() + i, key);
        node->rids.insert(node->rids.begin() + i, rid);

        if (static_cast<int>(node->keys.size()) < ORDER) return std::nullopt;
        return split_leaf(node);
    }

    // Internal node: find correct child and recurse
    int i = lower_bound(node->keys, key);
    if (i < static_cast<int>(node->keys.size()) && node->keys[i] == key) i++;
    auto child_split = insert_recursive(node->children[i], key, rid);

    if (!child_split) return std::nullopt;

    // Child was split — insert the pushed-up key into this internal node
    node->keys.insert(node->keys.begin() + i, child_split->push_up_key);
    node->children.insert(node->children.begin() + i + 1, child_split->right);

    if (static_cast<int>(node->keys.size()) < ORDER) return std::nullopt;
    return split_internal(node);
}

bool BPlusTree::remove_from_leaf(BPTNode* leaf, const Value& key) {
    int i = lower_bound(leaf->keys, key);
    if (i >= static_cast<int>(leaf->keys.size()) || leaf->keys[i] != key)
        return false;
    leaf->keys.erase(leaf->keys.begin() + i);
    leaf->rids.erase(leaf->rids.begin() + i);
    return true;
}
