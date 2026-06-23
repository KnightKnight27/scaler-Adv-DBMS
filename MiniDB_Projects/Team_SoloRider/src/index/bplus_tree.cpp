#include "bplus_tree.h"
#include <iostream>
#include <queue>
#include <cmath>
#include <algorithm>

namespace minidb {

BPlusTree::BPlusTree(int order) : order_(std::max(3, order)) {}

BPlusTree::~BPlusTree() {
    delete_tree(root_);
}

void BPlusTree::delete_tree(BPlusNode* node) {
    if (!node) return;
    if (!node->is_leaf) {
        for (auto child : node->children) {
            delete_tree(child);
        }
    }
    delete node;
}

bool BPlusTree::is_empty() const {
    return root_ == nullptr;
}

BPlusNode* BPlusTree::find_leaf(int key) {
    if (!root_) return nullptr;
    BPlusNode* curr = root_;
    while (!curr->is_leaf) {
        size_t i = 0;
        while (i < curr->keys.size() && key >= curr->keys[i]) {
            i++;
        }
        curr = curr->children[i];
    }
    return curr;
}

RecordId BPlusTree::search(int key) {
    BPlusNode* leaf = find_leaf(key);
    if (!leaf) return {INVALID_PAGE_ID, INVALID_SLOT_ID};
    
    for (size_t i = 0; i < leaf->keys.size(); i++) {
        if (leaf->keys[i] == key) {
            return leaf->values[i];
        }
    }
    return {INVALID_PAGE_ID, INVALID_SLOT_ID};
}

bool BPlusTree::insert(int key, RecordId rid) {
    if (!root_) {
        root_ = new BPlusNode();
        root_->is_leaf = true;
        root_->keys.push_back(key);
        root_->values.push_back(rid);
        return true;
    }

    BPlusNode* leaf = find_leaf(key);
    for (int k : leaf->keys) {
        if (k == key) return false; // No duplicates
    }

    insert_into_leaf(leaf, key, rid);
    
    if (leaf->keys.size() >= static_cast<size_t>(order_)) {
        split_leaf(leaf);
    }
    return true;
}

void BPlusTree::insert_into_leaf(BPlusNode* leaf, int key, RecordId rid) {
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    int dist = std::distance(leaf->keys.begin(), it);
    leaf->keys.insert(it, key);
    leaf->values.insert(leaf->values.begin() + dist, rid);
}

void BPlusTree::split_leaf(BPlusNode* leaf) {
    BPlusNode* new_leaf = new BPlusNode();
    new_leaf->is_leaf = true;
    
    int split_idx = std::ceil((order_ - 1) / 2.0);
    
    new_leaf->keys.assign(leaf->keys.begin() + split_idx, leaf->keys.end());
    new_leaf->values.assign(leaf->values.begin() + split_idx, leaf->values.end());
    
    leaf->keys.resize(split_idx);
    leaf->values.resize(split_idx);
    
    new_leaf->next = leaf->next;
    leaf->next = new_leaf;
    
    insert_into_parent(leaf, new_leaf->keys[0], new_leaf);
}

void BPlusTree::insert_into_parent(BPlusNode* old_node, int key, BPlusNode* new_node) {
    BPlusNode* parent = old_node->parent;
    if (!parent) {
        BPlusNode* new_root = new BPlusNode();
        new_root->is_leaf = false;
        new_root->keys.push_back(key);
        new_root->children.push_back(old_node);
        new_root->children.push_back(new_node);
        old_node->parent = new_root;
        new_node->parent = new_root;
        root_ = new_root;
        return;
    }

    auto it = std::lower_bound(parent->keys.begin(), parent->keys.end(), key);
    int dist = std::distance(parent->keys.begin(), it);
    
    parent->keys.insert(it, key);
    parent->children.insert(parent->children.begin() + dist + 1, new_node);
    new_node->parent = parent;
    
    if (parent->keys.size() >= static_cast<size_t>(order_)) {
        split_internal(parent);
    }
}

void BPlusTree::split_internal(BPlusNode* node) {
    BPlusNode* new_node = new BPlusNode();
    new_node->is_leaf = false;
    
    int split_idx = std::ceil((order_ - 1) / 2.0);
    int push_key = node->keys[split_idx];
    
    new_node->keys.assign(node->keys.begin() + split_idx + 1, node->keys.end());
    new_node->children.assign(node->children.begin() + split_idx + 1, node->children.end());
    
    for (auto child : new_node->children) {
        child->parent = new_node;
    }
    
    node->keys.resize(split_idx);
    node->children.resize(split_idx + 1);
    
    insert_into_parent(node, push_key, new_node);
}

bool BPlusTree::remove(int key) {
    if (!root_) return false;
    
    BPlusNode* leaf = find_leaf(key);
    auto it = std::find(leaf->keys.begin(), leaf->keys.end(), key);
    if (it == leaf->keys.end()) return false;
    
    int dist = std::distance(leaf->keys.begin(), it);
    leaf->keys.erase(it);
    leaf->values.erase(leaf->values.begin() + dist);
    
    if (leaf == root_) {
        if (leaf->keys.empty()) {
            delete root_;
            root_ = nullptr;
        }
        return true;
    }
    
    if (leaf->keys.size() < static_cast<size_t>(std::ceil((order_ - 1) / 2.0))) {
        handle_underflow(leaf);
    }
    return true;
}

void BPlusTree::handle_underflow(BPlusNode* node) {
    if (node == root_) {
        if (!node->is_leaf && node->keys.empty() && node->children.size() == 1) {
            root_ = node->children[0];
            root_->parent = nullptr;
            delete node;
        }
        return;
    }
    
    BPlusNode* parent = node->parent;
    int idx = find_sibling_index(parent, node);
    size_t min_keys = std::ceil((order_ - 1) / 2.0);
    
    // Borrow from left
    if (idx > 0 && parent->children[idx - 1]->keys.size() > min_keys) {
        BPlusNode* left = parent->children[idx - 1];
        if (node->is_leaf) {
            node->keys.insert(node->keys.begin(), left->keys.back());
            node->values.insert(node->values.begin(), left->values.back());
            left->keys.pop_back();
            left->values.pop_back();
            parent->keys[idx - 1] = node->keys[0];
        } else {
            node->keys.insert(node->keys.begin(), parent->keys[idx - 1]);
            node->children.insert(node->children.begin(), left->children.back());
            node->children[0]->parent = node;
            parent->keys[idx - 1] = left->keys.back();
            left->keys.pop_back();
            left->children.pop_back();
        }
        return;
    }
    
    // Borrow from right
    if (idx < static_cast<int>(parent->children.size()) - 1 && parent->children[idx + 1]->keys.size() > min_keys) {
        BPlusNode* right = parent->children[idx + 1];
        if (node->is_leaf) {
            node->keys.push_back(right->keys.front());
            node->values.push_back(right->values.front());
            right->keys.erase(right->keys.begin());
            right->values.erase(right->values.begin());
            parent->keys[idx] = right->keys[0];
        } else {
            node->keys.push_back(parent->keys[idx]);
            node->children.push_back(right->children.front());
            node->children.back()->parent = node;
            parent->keys[idx] = right->keys.front();
            right->keys.erase(right->keys.begin());
            right->children.erase(right->children.begin());
        }
        return;
    }
    
    // Merge
    if (idx > 0) {
        BPlusNode* left = parent->children[idx - 1];
        if (node->is_leaf) {
            left->keys.insert(left->keys.end(), node->keys.begin(), node->keys.end());
            left->values.insert(left->values.end(), node->values.begin(), node->values.end());
            left->next = node->next;
        } else {
            left->keys.push_back(parent->keys[idx - 1]);
            left->keys.insert(left->keys.end(), node->keys.begin(), node->keys.end());
            for (auto child : node->children) {
                child->parent = left;
            }
            left->children.insert(left->children.end(), node->children.begin(), node->children.end());
        }
        parent->keys.erase(parent->keys.begin() + idx - 1);
        parent->children.erase(parent->children.begin() + idx);
        delete node;
        node = left; // for underflow propagation logic if needed, although we propagate on parent
    } else {
        BPlusNode* right = parent->children[idx + 1];
        if (node->is_leaf) {
            node->keys.insert(node->keys.end(), right->keys.begin(), right->keys.end());
            node->values.insert(node->values.end(), right->values.begin(), right->values.end());
            node->next = right->next;
        } else {
            node->keys.push_back(parent->keys[idx]);
            node->keys.insert(node->keys.end(), right->keys.begin(), right->keys.end());
            for (auto child : right->children) {
                child->parent = node;
            }
            node->children.insert(node->children.end(), right->children.begin(), right->children.end());
        }
        parent->keys.erase(parent->keys.begin() + idx);
        parent->children.erase(parent->children.begin() + idx + 1);
        delete right;
        // node is kept
    }
    
    if (parent->keys.size() < static_cast<size_t>(std::ceil((order_ - 1) / 2.0))) {
        handle_underflow(parent);
    }
}

int BPlusTree::find_sibling_index(BPlusNode* parent, BPlusNode* child) {
    for (size_t i = 0; i < parent->children.size(); i++) {
        if (parent->children[i] == child) return i;
    }
    return -1;
}

std::vector<RecordId> BPlusTree::range_scan(int low_key, int high_key) {
    std::vector<RecordId> results;
    BPlusNode* leaf = find_leaf(low_key);
    while (leaf) {
        for (size_t i = 0; i < leaf->keys.size(); i++) {
            if (leaf->keys[i] >= low_key && leaf->keys[i] <= high_key) {
                results.push_back(leaf->values[i]);
            } else if (leaf->keys[i] > high_key) {
                return results;
            }
        }
        leaf = leaf->next;
    }
    return results;
}

void BPlusTree::print_tree() {
    if (!root_) {
        std::cout << "Empty Tree" << std::endl;
        return;
    }
    std::queue<BPlusNode*> q;
    q.push(root_);
    while (!q.empty()) {
        int level_size = q.size();
        for (int i = 0; i < level_size; i++) {
            BPlusNode* curr = q.front();
            q.pop();
            std::cout << "[";
            for (size_t k = 0; k < curr->keys.size(); k++) {
                std::cout << curr->keys[k];
                if (k < curr->keys.size() - 1) std::cout << ",";
            }
            std::cout << "] ";
            if (!curr->is_leaf) {
                for (auto child : curr->children) q.push(child);
            }
        }
        std::cout << std::endl;
    }
}

int BPlusTree::get_height() {
    if (!root_) return 0;
    int height = 1;
    BPlusNode* curr = root_;
    while (!curr->is_leaf) {
        height++;
        curr = curr->children[0];
    }
    return height;
}

} // namespace minidb
