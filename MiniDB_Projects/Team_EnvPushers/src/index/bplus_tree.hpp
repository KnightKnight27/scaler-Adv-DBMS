// In-memory B+ Tree mapping a key Value -> RID (unique keys).
//
// Used as the primary-key index: it accelerates equality and range lookups by
// turning an O(N) heap scan into an O(log N) descent plus a leaf-link walk.
//
// Design:
//   * Order ORDER: an internal node has up to ORDER children / ORDER-1 keys; a
//     leaf has up to ORDER-1 entries. Minimum occupancy is ceil(ORDER/2)-1.
//   * Leaves are singly linked (next_) so range scans walk left-to-right.
//   * Insert splits bottom-up; delete borrows from / merges with a sibling and
//     propagates underflow toward the root.
//
// It lives in memory and is rebuilt from the heap file on database open. That
// trade-off (vs. a paged on-disk B+ tree) is intentional: it keeps the index
// algorithms front-and-centre and easy to defend, at the cost of memory for
// very large tables. See README "Indexing" / "Limitations".
#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

#include "common/types.hpp"

namespace minidb {

class BPlusTree {
public:
    static constexpr int ORDER = 64;  // fanout

    BPlusTree() : root_(new Node(true)) {}
    ~BPlusTree() { destroy(root_); }

    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    // Insert or overwrite key -> rid. Returns false if key already existed
    // (value is still updated) -- useful for primary-key uniqueness checks.
    bool insert(const Value& key, const RID& rid) {
        Value up_key;
        Node* new_child = nullptr;
        bool existed = false;
        insert_rec(root_, key, rid, up_key, new_child, existed);
        if (new_child) {  // root split: grow a new level
            Node* new_root = new Node(false);
            new_root->keys.push_back(up_key);
            new_root->children.push_back(root_);
            new_root->children.push_back(new_child);
            root_ = new_root;
        }
        if (!existed) size_++;
        return !existed;
    }

    std::optional<RID> search(const Value& key) const {
        Node* leaf = find_leaf(key);
        int i = lower_bound(leaf->keys, key);
        if (i < (int)leaf->keys.size() && leaf->keys[i] == key)
            return leaf->values[i];
        return std::nullopt;
    }

    bool contains(const Value& key) const { return search(key).has_value(); }

    bool erase(const Value& key) {
        bool removed = erase_rec(root_, key);
        // Collapse root if it became an empty internal node.
        if (!root_->is_leaf && root_->keys.empty()) {
            Node* old = root_;
            root_ = root_->children[0];
            old->children.clear();
            delete old;
        }
        if (removed) size_--;
        return removed;
    }

    // Inclusive range scan [low, high]; either bound may be null (open-ended).
    void range_scan(const std::optional<Value>& low, const std::optional<Value>& high,
                    const std::function<void(const Value&, const RID&)>& fn) const {
        Node* leaf = low ? find_leaf(*low) : leftmost_leaf();
        while (leaf) {
            for (size_t i = 0; i < leaf->keys.size(); ++i) {
                const Value& k = leaf->keys[i];
                if (low && k.compare(*low) < 0) continue;
                if (high && k.compare(*high) > 0) return;
                fn(k, leaf->values[i]);
            }
            leaf = leaf->next;
        }
    }

    size_t size() const { return size_; }
    int height() const {
        int h = 1; Node* n = root_;
        while (!n->is_leaf) { n = n->children[0]; h++; }
        return h;
    }

private:
    struct Node {
        bool is_leaf;
        std::vector<Value> keys;
        std::vector<RID>   values;     // leaf only, parallel to keys
        std::vector<Node*> children;   // internal only, size = keys.size()+1
        Node* next = nullptr;          // leaf-link
        explicit Node(bool leaf) : is_leaf(leaf) {}
    };

    static int lower_bound(const std::vector<Value>& keys, const Value& key) {
        int lo = 0, hi = (int)keys.size();
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (keys[mid].compare(key) < 0) lo = mid + 1; else hi = mid;
        }
        return lo;
    }

    Node* find_leaf(const Value& key) const {
        Node* n = root_;
        while (!n->is_leaf) {
            int i = lower_bound(n->keys, key);
            // Descend right when key >= separator at i.
            if (i < (int)n->keys.size() && n->keys[i].compare(key) == 0) i++;
            n = n->children[i];
        }
        return n;
    }

    Node* leftmost_leaf() const {
        Node* n = root_;
        while (!n->is_leaf) n = n->children[0];
        return n;
    }

    static constexpr int min_keys() { return (ORDER + 1) / 2 - 1; }

    // Recursive insert. On split, sets new_child and up_key (separator pushed up).
    void insert_rec(Node* node, const Value& key, const RID& rid,
                    Value& up_key, Node*& new_child, bool& existed) {
        if (node->is_leaf) {
            int i = lower_bound(node->keys, key);
            if (i < (int)node->keys.size() && node->keys[i] == key) {
                node->values[i] = rid;  // overwrite
                existed = true;
                return;
            }
            node->keys.insert(node->keys.begin() + i, key);
            node->values.insert(node->values.begin() + i, rid);
            if ((int)node->keys.size() >= ORDER) split_leaf(node, up_key, new_child);
            return;
        }
        int i = lower_bound(node->keys, key);
        if (i < (int)node->keys.size() && node->keys[i].compare(key) == 0) i++;
        Value child_up;
        Node* child_new = nullptr;
        insert_rec(node->children[i], key, rid, child_up, child_new, existed);
        if (child_new) {
            node->keys.insert(node->keys.begin() + i, child_up);
            node->children.insert(node->children.begin() + i + 1, child_new);
            if ((int)node->keys.size() >= ORDER) split_internal(node, up_key, new_child);
        }
    }

    void split_leaf(Node* leaf, Value& up_key, Node*& new_child) {
        int mid = (int)leaf->keys.size() / 2;
        Node* right = new Node(true);
        right->keys.assign(leaf->keys.begin() + mid, leaf->keys.end());
        right->values.assign(leaf->values.begin() + mid, leaf->values.end());
        leaf->keys.resize(mid);
        leaf->values.resize(mid);
        right->next = leaf->next;
        leaf->next = right;
        up_key = right->keys.front();   // copy-up
        new_child = right;
    }

    void split_internal(Node* node, Value& up_key, Node*& new_child) {
        int mid = (int)node->keys.size() / 2;
        up_key = node->keys[mid];        // push-up (removed from this level)
        Node* right = new Node(false);
        right->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
        right->children.assign(node->children.begin() + mid + 1, node->children.end());
        node->keys.resize(mid);
        node->children.resize(mid + 1);
        new_child = right;
    }

    // Returns true if a key was removed somewhere in this subtree.
    bool erase_rec(Node* node, const Value& key) {
        if (node->is_leaf) {
            int i = lower_bound(node->keys, key);
            if (i >= (int)node->keys.size() || !(node->keys[i] == key)) return false;
            node->keys.erase(node->keys.begin() + i);
            node->values.erase(node->values.begin() + i);
            return true;
        }
        int i = lower_bound(node->keys, key);
        if (i < (int)node->keys.size() && node->keys[i].compare(key) == 0) i++;
        bool removed = erase_rec(node->children[i], key);
        if (removed && (int)node->children[i]->keys.size() < min_keys())
            rebalance(node, i);
        return removed;
    }

    // Fix underflow of node->children[idx] by borrowing or merging.
    void rebalance(Node* parent, int idx) {
        Node* child = parent->children[idx];
        // Try borrow from left sibling.
        if (idx > 0 && (int)parent->children[idx - 1]->keys.size() > min_keys()) {
            borrow_from_left(parent, idx);
        } else if (idx < (int)parent->children.size() - 1 &&
                   (int)parent->children[idx + 1]->keys.size() > min_keys()) {
            borrow_from_right(parent, idx);
        } else if (idx > 0) {
            merge(parent, idx - 1);  // merge child into left sibling
        } else {
            merge(parent, idx);      // merge right sibling into child
        }
        (void)child;
    }

    void borrow_from_left(Node* parent, int idx) {
        Node* child = parent->children[idx];
        Node* left = parent->children[idx - 1];
        if (child->is_leaf) {
            child->keys.insert(child->keys.begin(), left->keys.back());
            child->values.insert(child->values.begin(), left->values.back());
            left->keys.pop_back();
            left->values.pop_back();
            parent->keys[idx - 1] = child->keys.front();
        } else {
            child->keys.insert(child->keys.begin(), parent->keys[idx - 1]);
            child->children.insert(child->children.begin(), left->children.back());
            parent->keys[idx - 1] = left->keys.back();
            left->keys.pop_back();
            left->children.pop_back();
        }
    }

    void borrow_from_right(Node* parent, int idx) {
        Node* child = parent->children[idx];
        Node* right = parent->children[idx + 1];
        if (child->is_leaf) {
            child->keys.push_back(right->keys.front());
            child->values.push_back(right->values.front());
            right->keys.erase(right->keys.begin());
            right->values.erase(right->values.begin());
            parent->keys[idx] = right->keys.front();
        } else {
            child->keys.push_back(parent->keys[idx]);
            child->children.push_back(right->children.front());
            parent->keys[idx] = right->keys.front();
            right->keys.erase(right->keys.begin());
            right->children.erase(right->children.begin());
        }
    }

    // Merge children[idx+1] into children[idx], pulling down separator keys[idx].
    void merge(Node* parent, int idx) {
        Node* left = parent->children[idx];
        Node* right = parent->children[idx + 1];
        if (left->is_leaf) {
            left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
            left->values.insert(left->values.end(), right->values.begin(), right->values.end());
            left->next = right->next;
        } else {
            left->keys.push_back(parent->keys[idx]);
            left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
            left->children.insert(left->children.end(),
                                  right->children.begin(), right->children.end());
        }
        parent->keys.erase(parent->keys.begin() + idx);
        parent->children.erase(parent->children.begin() + idx + 1);
        delete right;
    }

    static void destroy(Node* n) {
        if (!n) return;
        if (!n->is_leaf)
            for (Node* c : n->children) destroy(c);
        delete n;
    }

    Node* root_;
    size_t size_ = 0;
};

}  // namespace minidb
