#pragma once
/**
 * Lab 4 — B-Tree Implementation
 *
 * A balanced tree data structure optimized for disk-based storage.
 * Supports: insert (with split), search, delete (with borrow & merge).
 *
 * B-Tree of order M (minimum degree t):
 *   - Each node has at most 2t-1 keys and 2t children
 *   - Each non-root node has at least t-1 keys
 *   - Root has at least 1 key (if non-empty)
 *   - All leaves are at the same level
 */

#include <iostream>
#include <vector>
#include <algorithm>
#include <functional>
#include <string>
#include <queue>
#include <iomanip>
#include <cassert>

template <typename K>
class BTree {
private:
    struct Node {
        std::vector<K>     keys;
        std::vector<Node*> children;
        bool               leaf;

        Node(bool is_leaf) : leaf(is_leaf) {}

        ~Node() {
            for (auto* child : children) {
                delete child;
            }
        }

        int key_count() const { return static_cast<int>(keys.size()); }
    };

    Node* root_;
    int   t_;     // minimum degree (each node has [t-1, 2t-1] keys, except root)
    int   size_;  // total number of keys

    // ─── Search in subtree ───
    Node* search_impl(Node* node, const K& key, int& idx) const {
        if (!node) return nullptr;

        int i = 0;
        while (i < node->key_count() && key > node->keys[i]) i++;

        if (i < node->key_count() && key == node->keys[i]) {
            idx = i;
            return node;
        }

        if (node->leaf) return nullptr;
        return search_impl(node->children[i], key, idx);
    }

    // ─── Split a full child ───
    // node->children[idx] is full (has 2t-1 keys). Split it into two nodes.
    void split_child(Node* parent, int idx) {
        Node* full_child = parent->children[idx];
        Node* new_child = new Node(full_child->leaf);

        int mid = t_ - 1;

        // Move the upper t-1 keys to new_child
        for (int j = mid + 1; j < full_child->key_count(); j++) {
            new_child->keys.push_back(full_child->keys[j]);
        }

        // Move the upper t children to new_child (if not leaf)
        if (!full_child->leaf) {
            for (int j = mid + 1; j <= static_cast<int>(full_child->children.size()) - 1; j++) {
                new_child->children.push_back(full_child->children[j]);
            }
            full_child->children.resize(mid + 1);
        }

        // Promote the median key to parent
        K median = full_child->keys[mid];
        full_child->keys.resize(mid);

        // Insert new_child and median into parent
        parent->children.insert(parent->children.begin() + idx + 1, new_child);
        parent->keys.insert(parent->keys.begin() + idx, median);
    }

    // ─── Insert into non-full node ───
    void insert_non_full(Node* node, const K& key) {
        int i = node->key_count() - 1;

        if (node->leaf) {
            // Find position and insert
            node->keys.push_back(K{});
            while (i >= 0 && key < node->keys[i]) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }
            node->keys[i + 1] = key;
        } else {
            // Find the child to descend into
            while (i >= 0 && key < node->keys[i]) i--;
            i++;

            // If child is full, split it first
            if (node->children[i]->key_count() == 2 * t_ - 1) {
                split_child(node, i);
                if (key > node->keys[i]) i++;
            }
            insert_non_full(node->children[i], key);
        }
    }

    // ─── Delete helpers ───

    // Find the predecessor key (rightmost key in left subtree)
    K get_predecessor(Node* node, int idx) {
        Node* cur = node->children[idx];
        while (!cur->leaf) cur = cur->children.back();
        return cur->keys.back();
    }

    // Find the successor key (leftmost key in right subtree)
    K get_successor(Node* node, int idx) {
        Node* cur = node->children[idx + 1];
        while (!cur->leaf) cur = cur->children[0];
        return cur->keys[0];
    }

    // Borrow a key from the left sibling
    void borrow_from_left(Node* parent, int idx) {
        Node* child = parent->children[idx];
        Node* left_sibling = parent->children[idx - 1];

        // Shift all keys in child right by 1
        child->keys.insert(child->keys.begin(), parent->keys[idx - 1]);

        // Move the rightmost key of left sibling up to parent
        parent->keys[idx - 1] = left_sibling->keys.back();
        left_sibling->keys.pop_back();

        // Move the rightmost child of left sibling to child
        if (!child->leaf) {
            child->children.insert(child->children.begin(), left_sibling->children.back());
            left_sibling->children.pop_back();
        }
    }

    // Borrow a key from the right sibling
    void borrow_from_right(Node* parent, int idx) {
        Node* child = parent->children[idx];
        Node* right_sibling = parent->children[idx + 1];

        // Move parent key down to child
        child->keys.push_back(parent->keys[idx]);

        // Move the leftmost key of right sibling up to parent
        parent->keys[idx] = right_sibling->keys[0];
        right_sibling->keys.erase(right_sibling->keys.begin());

        // Move the leftmost child of right sibling to child
        if (!child->leaf) {
            child->children.push_back(right_sibling->children[0]);
            right_sibling->children.erase(right_sibling->children.begin());
        }
    }

    // Merge child[idx] with child[idx+1], pulling parent key down
    void merge(Node* parent, int idx) {
        Node* left = parent->children[idx];
        Node* right = parent->children[idx + 1];

        // Pull parent key down into left
        left->keys.push_back(parent->keys[idx]);

        // Move all keys from right into left
        for (auto& k : right->keys) left->keys.push_back(k);

        // Move all children from right into left
        if (!right->leaf) {
            for (auto* c : right->children) left->children.push_back(c);
        }

        // Remove the parent key and right child pointer
        parent->keys.erase(parent->keys.begin() + idx);
        parent->children.erase(parent->children.begin() + idx + 1);

        // Don't recursively delete right's children (they've been moved to left)
        right->children.clear();
        delete right;
    }

    // Ensure child has at least t keys (fill if needed)
    void fill(Node* parent, int idx) {
        if (idx > 0 && parent->children[idx - 1]->key_count() >= t_) {
            borrow_from_left(parent, idx);
        } else if (idx < static_cast<int>(parent->children.size()) - 1 &&
                   parent->children[idx + 1]->key_count() >= t_) {
            borrow_from_right(parent, idx);
        } else {
            // Merge with a sibling
            if (idx < static_cast<int>(parent->children.size()) - 1) {
                merge(parent, idx);
            } else {
                merge(parent, idx - 1);
            }
        }
    }

    // Delete key from subtree rooted at node
    void remove_impl(Node* node, const K& key) {
        int idx = 0;
        while (idx < node->key_count() && node->keys[idx] < key) idx++;

        if (idx < node->key_count() && node->keys[idx] == key) {
            // Key found in this node
            if (node->leaf) {
                // Case 1: Key is in a leaf — simply remove it
                node->keys.erase(node->keys.begin() + idx);
            } else {
                // Case 2: Key is in an internal node
                if (node->children[idx]->key_count() >= t_) {
                    // Case 2a: Left child has enough keys — replace with predecessor
                    K pred = get_predecessor(node, idx);
                    node->keys[idx] = pred;
                    remove_impl(node->children[idx], pred);
                } else if (node->children[idx + 1]->key_count() >= t_) {
                    // Case 2b: Right child has enough keys — replace with successor
                    K succ = get_successor(node, idx);
                    node->keys[idx] = succ;
                    remove_impl(node->children[idx + 1], succ);
                } else {
                    // Case 2c: Both children have t-1 keys — merge them
                    merge(node, idx);
                    remove_impl(node->children[idx], key);
                }
            }
        } else {
            // Key not in this node — must be in a child
            if (node->leaf) return;  // key not found

            bool last_child = (idx == node->key_count());

            // Ensure the child we descend into has at least t keys
            if (node->children[idx]->key_count() < t_) {
                fill(node, idx);
            }

            // After fill, idx might have changed if we merged with left sibling
            if (last_child && idx > node->key_count()) {
                remove_impl(node->children[idx - 1], key);
            } else {
                remove_impl(node->children[idx], key);
            }
        }
    }

    // ─── Traversal ───
    void inorder_impl(Node* node, std::function<void(const K&)> visit) const {
        if (!node) return;
        for (int i = 0; i < node->key_count(); i++) {
            if (!node->leaf) inorder_impl(node->children[i], visit);
            visit(node->keys[i]);
        }
        if (!node->leaf) inorder_impl(node->children.back(), visit);
    }

    // ─── Verify B-Tree properties ───
    int verify_impl(Node* node, int depth, int& leaf_depth) const {
        if (!node) return 0;

        // Check key count bounds
        if (node != root_) {
            if (node->key_count() < t_ - 1) {
                std::cerr << "VIOLATION: Node has " << node->key_count()
                          << " keys, minimum is " << t_ - 1 << std::endl;
                return -1;
            }
        }
        if (node->key_count() > 2 * t_ - 1) {
            std::cerr << "VIOLATION: Node has " << node->key_count()
                      << " keys, maximum is " << 2 * t_ - 1 << std::endl;
            return -1;
        }

        // Check keys are sorted
        for (int i = 1; i < node->key_count(); i++) {
            if (node->keys[i] <= node->keys[i - 1]) {
                std::cerr << "VIOLATION: Keys not sorted!" << std::endl;
                return -1;
            }
        }

        // Check children count
        if (!node->leaf) {
            if (static_cast<int>(node->children.size()) != node->key_count() + 1) {
                std::cerr << "VIOLATION: Wrong number of children!" << std::endl;
                return -1;
            }
        }

        // Check leaf depth consistency
        if (node->leaf) {
            if (leaf_depth == -1) leaf_depth = depth;
            else if (depth != leaf_depth) {
                std::cerr << "VIOLATION: Unequal leaf depths!" << std::endl;
                return -1;
            }
        }

        int count = node->key_count();
        if (!node->leaf) {
            for (auto* child : node->children) {
                int c = verify_impl(child, depth + 1, leaf_depth);
                if (c == -1) return -1;
                count += c;
            }
        }
        return count;
    }

public:
    explicit BTree(int min_degree = 3) : root_(nullptr), t_(min_degree), size_(0) {
        assert(t_ >= 2);
    }

    ~BTree() {
        delete root_;
    }

    // ─── Insert ───
    void insert(const K& key) {
        if (!root_) {
            root_ = new Node(true);
            root_->keys.push_back(key);
        } else {
            if (root_->key_count() == 2 * t_ - 1) {
                // Root is full — create new root and split
                Node* new_root = new Node(false);
                new_root->children.push_back(root_);
                root_ = new_root;
                split_child(root_, 0);
                insert_non_full(root_, key);
            } else {
                insert_non_full(root_, key);
            }
        }
        size_++;
    }

    // ─── Search ───
    bool search(const K& key) const {
        int idx;
        return search_impl(root_, key, idx) != nullptr;
    }

    // ─── Delete ───
    bool remove(const K& key) {
        if (!root_) return false;

        // Check if key exists first
        int idx;
        if (!search_impl(root_, key, idx)) return false;

        remove_impl(root_, key);

        // If root has no keys and has a child, shrink the tree
        if (root_->key_count() == 0) {
            Node* old_root = root_;
            if (root_->leaf) {
                root_ = nullptr;
            } else {
                root_ = root_->children[0];
                old_root->children.clear();  // prevent recursive delete
            }
            delete old_root;
        }

        size_--;
        return true;
    }

    // ─── Traversal ───
    void inorder(std::function<void(const K&)> visit) const {
        inorder_impl(root_, visit);
    }

    // ─── Verify ───
    bool verify() const {
        if (!root_) return true;
        int leaf_depth = -1;
        return verify_impl(root_, 0, leaf_depth) != -1;
    }

    // ─── Print tree structure ───
    void print() const {
        if (!root_) {
            std::cout << "  (empty tree)" << std::endl;
            return;
        }

        std::queue<std::pair<Node*, int>> q;
        q.push({root_, 0});
        int prev_level = -1;

        while (!q.empty()) {
            auto [node, level] = q.front(); q.pop();
            if (level != prev_level) {
                if (prev_level >= 0) std::cout << std::endl;
                std::cout << "  Level " << level << ": ";
                prev_level = level;
            }

            std::cout << "[";
            for (int i = 0; i < node->key_count(); i++) {
                if (i > 0) std::cout << ",";
                std::cout << node->keys[i];
            }
            std::cout << "] ";

            if (!node->leaf) {
                for (auto* child : node->children) {
                    q.push({child, level + 1});
                }
            }
        }
        std::cout << std::endl;
    }

    int size() const { return size_; }
    bool empty() const { return root_ == nullptr; }
    int min_degree() const { return t_; }
};
