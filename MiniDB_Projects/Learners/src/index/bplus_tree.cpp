#include "bplus_tree.h"
#include <algorithm>

BPlusNode::~BPlusNode() {
    if (!is_leaf) {
        for (auto* child : children) {
            delete child;
        }
    }
}

BPlusTree::BPlusTree(int order) : order(order) {
    root = new BPlusNode(true);
}

BPlusTree::~BPlusTree() {
    delete root;
}

bool BPlusTree::search(int key, std::pair<int, int>& value) const {
    BPlusNode* node = root;
    while (!node->is_leaf) {
        int idx = 0;
        while (idx < node->keys.size() && key >= node->keys[idx]) {
            idx++;
        }
        node = node->children[idx];
    }

    for (size_t i = 0; i < node->keys.size(); ++i) {
        if (node->keys[i] == key) {
            value = node->leaf_values[i];
            return true;
        }
    }
    return false;
}

std::vector<std::pair<int, std::pair<int, int>>> BPlusTree::range_search(int start_key, int end_key) const {
    std::vector<std::pair<int, std::pair<int, int>>> results;
    BPlusNode* node = root;
    
    // 1. Traverse down to leaf node
    while (!node->is_leaf) {
        int idx = 0;
        while (idx < node->keys.size() && start_key >= node->keys[idx]) {
            idx++;
        }
        node = node->children[idx];
    }

    // 2. Scan leaf chain
    while (node != nullptr) {
        for (size_t i = 0; i < node->keys.size(); ++i) {
            int k = node->keys[i];
            if (k >= start_key && k <= end_key) {
                results.push_back({k, node->leaf_values[i]});
            } else if (k > end_key) {
                return results;
            }
        }
        node = node->next;
    }
    return results;
}

bool BPlusTree::insert(int key, std::pair<int, int> value) {
    std::pair<int, int> dummy;
    if (search(key, dummy)) {
        return false; // Key already exists (primary key constraint)
    }

    BPlusNode* r = root;
    if (r->keys.size() == order - 1) {
        BPlusNode* s = new BPlusNode(false);
        s->children.push_back(root);
        split_child(s, 0);
        root = s;
    }
    insert_non_full(root, key, value);
    return true;
}

void BPlusTree::insert_non_full(BPlusNode* node, int key, std::pair<int, int> value) {
    if (node->is_leaf) {
        int idx = 0;
        while (idx < node->keys.size() && key > node->keys[idx]) {
            idx++;
        }
        node->keys.insert(node->keys.begin() + idx, key);
        node->leaf_values.insert(node->leaf_values.begin() + idx, value);
    } else {
        int idx = node->keys.size() - 1;
        while (idx >= 0 && key < node->keys[idx]) {
            idx--;
        }
        idx++;
        BPlusNode* child = node->children[idx];
        if (child->keys.size() == order - 1) {
            split_child(node, idx);
            if (key > node->keys[idx]) {
                idx++;
            }
        }
        insert_non_full(node->children[idx], key, value);
    }
}

void BPlusTree::split_child(BPlusNode* parent, int idx) {
    BPlusNode* child = parent->children[idx];
    BPlusNode* new_child = new BPlusNode(child->is_leaf);

    int mid = child->keys.size() / 2;
    int split_key = child->keys[mid];

    if (child->is_leaf) {
        new_child->keys.assign(child->keys.begin() + mid, child->keys.end());
        new_child->leaf_values.assign(child->leaf_values.begin() + mid, child->leaf_values.end());
        
        child->keys.erase(child->keys.begin() + mid, child->keys.end());
        child->leaf_values.erase(child->leaf_values.begin() + mid, child->leaf_values.end());

        new_child->next = child->next;
        child->next = new_child;
    } else {
        new_child->keys.assign(child->keys.begin() + mid + 1, child->keys.end());
        new_child->children.assign(child->children.begin() + mid + 1, child->children.end());

        child->keys.erase(child->keys.begin() + mid, child->keys.end());
        child->children.erase(child->children.begin() + mid + 1, child->children.end());
    }

    parent->keys.insert(parent->keys.begin() + idx, split_key);
    parent->children.insert(parent->children.begin() + idx + 1, new_child);
}

bool BPlusTree::delete_key(int key) {
    bool deleted = delete_helper(root, key);
    if (deleted && root->keys.empty() && !root->is_leaf) {
        BPlusNode* old_root = root;
        root = root->children[0];
        old_root->children.clear(); // Prevent deleting the new root node
        delete old_root;
    }
    return deleted;
}

bool BPlusTree::delete_helper(BPlusNode* node, int key) {
    if (node->is_leaf) {
        auto it = std::find(node->keys.begin(), node->keys.end(), key);
        if (it != node->keys.end()) {
            int idx = std::distance(node->keys.begin(), it);
            node->keys.erase(node->keys.begin() + idx);
            node->leaf_values.erase(node->leaf_values.begin() + idx);
            return true;
        }
        return false;
    }

    int idx = 0;
    while (idx < node->keys.size() && key >= node->keys[idx]) {
        idx++;
    }

    BPlusNode* child = node->children[idx];
    bool deleted = delete_helper(child, key);

    int min_keys = (order / 2) - 1;
    if (deleted && child->keys.size() < min_keys) {
        handle_underflow(node, idx);
    }
    return deleted;
}

void BPlusTree::handle_underflow(BPlusNode* parent, int idx) {
    BPlusNode* child = parent->children[idx];
    int min_keys = (order / 2) - 1;

    // 1. Try to borrow from left sibling
    if (idx > 0) {
        BPlusNode* left = parent->children[idx - 1];
        if (left->keys.size() > min_keys) {
            if (child->is_leaf) {
                child->keys.insert(child->keys.begin(), left->keys.back());
                child->leaf_values.insert(child->leaf_values.begin(), left->leaf_values.back());
                left->keys.pop_back();
                left->leaf_values.pop_back();
                parent->keys[idx - 1] = child->keys[0];
            } else {
                child->keys.insert(child->keys.begin(), parent->keys[idx - 1]);
                child->children.insert(child->children.begin(), left->children.back());
                parent->keys[idx - 1] = left->keys.back();
                left->keys.pop_back();
                left->children.pop_back();
            }
            return;
        }
    }

    // 2. Try to borrow from right sibling
    if (idx < parent->children.size() - 1) {
        BPlusNode* right = parent->children[idx + 1];
        if (right->keys.size() > min_keys) {
            if (child->is_leaf) {
                child->keys.push_back(right->keys.front());
                child->leaf_values.push_back(right->leaf_values.front());
                right->keys.erase(right->keys.begin());
                right->leaf_values.erase(right->leaf_values.begin());
                parent->keys[idx] = right->keys[0];
            } else {
                child->keys.push_back(parent->keys[idx]);
                child->children.push_back(right->children.front());
                parent->keys[idx] = right->keys.front();
                right->keys.erase(right->keys.begin());
                right->children.erase(right->children.begin());
            }
            return;
        }
    }

    // 3. Merge with sibling
    if (idx > 0) {
        // Merge with left sibling
        BPlusNode* left = parent->children[idx - 1];
        if (child->is_leaf) {
            left->keys.insert(left->keys.end(), child->keys.begin(), child->keys.end());
            left->leaf_values.insert(left->leaf_values.end(), child->leaf_values.begin(), child->leaf_values.end());
            left->next = child->next;
        } else {
            left->keys.push_back(parent->keys[idx - 1]);
            left->keys.insert(left->keys.end(), child->keys.begin(), child->keys.end());
            left->children.insert(left->children.end(), child->children.begin(), child->children.end());
        }
        parent->keys.erase(parent->keys.begin() + idx - 1);
        parent->children.erase(parent->children.begin() + idx);
        
        // Deallocate child safely
        child->children.clear();
        delete child;
    } else {
        // Merge with right sibling
        BPlusNode* right = parent->children[idx + 1];
        if (child->is_leaf) {
            child->keys.insert(child->keys.end(), right->keys.begin(), right->keys.end());
            child->leaf_values.insert(child->leaf_values.end(), right->leaf_values.begin(), right->leaf_values.end());
            child->next = right->next;
        } else {
            child->keys.push_back(parent->keys[idx]);
            child->keys.insert(child->keys.end(), right->keys.begin(), right->keys.end());
            child->children.insert(child->children.end(), right->children.begin(), right->children.end());
        }
        parent->keys.erase(parent->keys.begin() + idx);
        parent->children.erase(parent->children.begin() + idx + 1);

        // Deallocate right sibling safely
        right->children.clear();
        delete right;
    }
}
