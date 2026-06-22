#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../types.h"

namespace minidb {

// In-memory B+ Tree mapping an int64 key to a RID. Internal nodes hold
// separator keys only; all values live in the linked leaf level. Keys are
// unique (an insert of an existing key overwrites its value).
class BPlusTree {
public:
    explicit BPlusTree(int order = 64) : order_(order) {}
    ~BPlusTree() { destroy(root_); }

    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    bool search(int64_t key, RID& out) const {
        Node* n = root_;
        if (!n) return false;
        while (!n->leaf) n = n->child[child_index(n, key)];
        for (size_t i = 0; i < n->keys.size(); ++i)
            if (n->keys[i] == key) {
                out = n->vals[i];
                return true;
            }
        return false;
    }

    void insert(int64_t key, RID rid) {
        if (!root_) {
            root_ = new Node(true);
            root_->keys.push_back(key);
            root_->vals.push_back(rid);
            return;
        }
        Split s = insert_rec(root_, key, rid);
        if (s.split) {
            Node* nr = new Node(false);
            nr->keys.push_back(s.key);
            nr->child.push_back(root_);
            nr->child.push_back(s.right);
            root_ = nr;
        }
    }

    bool erase(int64_t key) {
        if (!root_) return false;
        bool removed = erase_rec(root_, key);
        if (removed && !root_->leaf && root_->keys.empty()) {
            Node* old = root_;
            root_ = root_->child[0];
            old->child.clear();
            delete old;
        }
        if (removed && root_->leaf && root_->keys.empty()) {
            delete root_;
            root_ = nullptr;
        }
        return removed;
    }

private:
    struct Node {
        bool leaf;
        std::vector<int64_t> keys;
        std::vector<RID> vals;     // leaf only
        std::vector<Node*> child;  // internal only
        Node* next = nullptr;      // leaf sibling chain
        explicit Node(bool l) : leaf(l) {}
    };

    struct Split {
        bool split = false;
        int64_t key = 0;
        Node* right = nullptr;
    };

    int order_;  // maximum number of keys per node
    Node* root_ = nullptr;

    int min_keys() const { return order_ / 2; }

    static int child_index(Node* n, int64_t key) {
        int i = 0;
        while (i < static_cast<int>(n->keys.size()) && key >= n->keys[i]) ++i;
        return i;
    }

    Split insert_rec(Node* n, int64_t key, RID rid) {
        if (n->leaf) {
            int i = static_cast<int>(std::lower_bound(n->keys.begin(), n->keys.end(), key) -
                                     n->keys.begin());
            if (i < static_cast<int>(n->keys.size()) && n->keys[i] == key) {
                n->vals[i] = rid;
                return {};
            }
            n->keys.insert(n->keys.begin() + i, key);
            n->vals.insert(n->vals.begin() + i, rid);
            if (static_cast<int>(n->keys.size()) <= order_) return {};

            int mid = static_cast<int>(n->keys.size()) / 2;
            Node* r = new Node(true);
            r->keys.assign(n->keys.begin() + mid, n->keys.end());
            r->vals.assign(n->vals.begin() + mid, n->vals.end());
            n->keys.resize(mid);
            n->vals.resize(mid);
            r->next = n->next;
            n->next = r;
            return {true, r->keys.front(), r};
        }

        int i = child_index(n, key);
        Split s = insert_rec(n->child[i], key, rid);
        if (!s.split) return {};
        n->keys.insert(n->keys.begin() + i, s.key);
        n->child.insert(n->child.begin() + i + 1, s.right);
        if (static_cast<int>(n->keys.size()) <= order_) return {};

        int mid = static_cast<int>(n->keys.size()) / 2;
        int64_t up = n->keys[mid];
        Node* r = new Node(false);
        r->keys.assign(n->keys.begin() + mid + 1, n->keys.end());
        r->child.assign(n->child.begin() + mid + 1, n->child.end());
        n->keys.resize(mid);
        n->child.resize(mid + 1);
        return {true, up, r};
    }

    bool erase_rec(Node* n, int64_t key) {
        if (n->leaf) {
            int i = static_cast<int>(std::lower_bound(n->keys.begin(), n->keys.end(), key) -
                                     n->keys.begin());
            if (i >= static_cast<int>(n->keys.size()) || n->keys[i] != key) return false;
            n->keys.erase(n->keys.begin() + i);
            n->vals.erase(n->vals.begin() + i);
            return true;
        }
        int i = child_index(n, key);
        bool removed = erase_rec(n->child[i], key);
        if (!removed) return false;
        if (static_cast<int>(n->child[i]->keys.size()) < min_keys()) fix_underflow(n, i);
        return true;
    }

    void fix_underflow(Node* parent, int i) {
        Node* child = parent->child[i];
        Node* left = i > 0 ? parent->child[i - 1] : nullptr;
        Node* right = i + 1 < static_cast<int>(parent->child.size()) ? parent->child[i + 1] : nullptr;

        if (left && static_cast<int>(left->keys.size()) > min_keys()) {
            borrow_from_left(parent, i, child, left);
        } else if (right && static_cast<int>(right->keys.size()) > min_keys()) {
            borrow_from_right(parent, i, child, right);
        } else if (left) {
            merge(parent, i - 1, left, child);
        } else {
            merge(parent, i, child, right);
        }
    }

    void borrow_from_left(Node* parent, int i, Node* child, Node* left) {
        if (child->leaf) {
            child->keys.insert(child->keys.begin(), left->keys.back());
            child->vals.insert(child->vals.begin(), left->vals.back());
            left->keys.pop_back();
            left->vals.pop_back();
            parent->keys[i - 1] = child->keys.front();
        } else {
            child->keys.insert(child->keys.begin(), parent->keys[i - 1]);
            child->child.insert(child->child.begin(), left->child.back());
            parent->keys[i - 1] = left->keys.back();
            left->keys.pop_back();
            left->child.pop_back();
        }
    }

    void borrow_from_right(Node* parent, int i, Node* child, Node* right) {
        if (child->leaf) {
            child->keys.push_back(right->keys.front());
            child->vals.push_back(right->vals.front());
            right->keys.erase(right->keys.begin());
            right->vals.erase(right->vals.begin());
            parent->keys[i] = right->keys.front();
        } else {
            child->keys.push_back(parent->keys[i]);
            child->child.push_back(right->child.front());
            parent->keys[i] = right->keys.front();
            right->keys.erase(right->keys.begin());
            right->child.erase(right->child.begin());
        }
    }

    // Merge child[i+1] into child[i]; parent separator at index i is consumed.
    void merge(Node* parent, int i, Node* left, Node* right) {
        if (left->leaf) {
            left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
            left->vals.insert(left->vals.end(), right->vals.begin(), right->vals.end());
            left->next = right->next;
        } else {
            left->keys.push_back(parent->keys[i]);
            left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
            left->child.insert(left->child.end(), right->child.begin(), right->child.end());
        }
        parent->keys.erase(parent->keys.begin() + i);
        parent->child.erase(parent->child.begin() + i + 1);
        delete right;
    }

    static void destroy(Node* n) {
        if (!n) return;
        if (!n->leaf)
            for (Node* c : n->child) destroy(c);
        delete n;
    }
};

}  // namespace minidb
