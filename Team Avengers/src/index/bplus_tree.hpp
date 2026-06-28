// ============================================================================
//  bplus_tree.hpp — Primary-key index mapping  key(int64) -> RID.
//
//  This is the Lab 6 B-tree upgraded to a real B+ TREE, which is what databases
//  actually use for indexes. Two structural differences from a plain B-tree:
//
//    1. ALL keys/values live in the LEAVES. Internal nodes hold only separator
//       keys to route a search downward. So every search touches a leaf — the
//       cost of every lookup is uniform (= tree height).
//    2. Leaves are linked left-to-right. A range scan (WHERE id BETWEEN ...)
//       finds the start leaf, then walks `next` pointers — no re-descent.
//
//  We store the index in memory and rebuild it from the heap file on startup.
//  That is a deliberate trade-off for the time budget: it keeps the node code
//  free of page-pinning, while still demonstrating real B+ tree mechanics
//  (splits on insert, borrow/merge on delete). Documented in Limitations.
// ============================================================================
#pragma once

#include "../common/types.hpp"

#include <algorithm>
#include <vector>

namespace minidb {

class BPlusTree {
public:
    using KeyT = int64_t;

    // ORDER = max children per internal node. A node splits when it would hold
    // ORDER keys. 4 is small on purpose: even a tiny table exercises splits,
    // which makes the structure observable in a demo / debugger.
    explicit BPlusTree(int order = 4) : order_(order) {
        root_ = new Node(/*leaf=*/true);
    }
    ~BPlusTree() { destroy(root_); }

    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    // Point lookup. Returns true and fills *out if the key exists.
    bool search(KeyT key, RID* out) const {
        Node* leaf = find_leaf(key);
        int i = lower_bound_idx(leaf->keys, key);
        if (i < (int)leaf->keys.size() && leaf->keys[i] == key) {
            if (out) *out = leaf->vals[i];
            return true;
        }
        return false;
    }

    // Insert key->rid. Duplicate primary keys are rejected (returns false),
    // because this is a PRIMARY index — keys are unique by definition.
    bool insert(KeyT key, RID rid) {
        Node* leaf = find_leaf(key);
        int i = lower_bound_idx(leaf->keys, key);
        if (i < (int)leaf->keys.size() && leaf->keys[i] == key) return false;

        leaf->keys.insert(leaf->keys.begin() + i, key);
        leaf->vals.insert(leaf->vals.begin() + i, rid);

        if ((int)leaf->keys.size() >= order_) split_leaf(leaf);
        return true;
    }

    // Remove a key. Returns false if absent. Handles leaf underflow by
    // borrowing from a sibling, else merging — the standard B+ tree delete.
    bool erase(KeyT key) {
        Node* leaf = find_leaf(key);
        int i = lower_bound_idx(leaf->keys, key);
        if (i >= (int)leaf->keys.size() || leaf->keys[i] != key) return false;
        leaf->keys.erase(leaf->keys.begin() + i);
        leaf->vals.erase(leaf->vals.begin() + i);
        if (leaf != root_ && (int)leaf->keys.size() < min_keys())
            rebalance(leaf);
        return true;
    }

    // Range scan [lo, hi] inclusive, in key order. This is the operation that
    // justifies a B+ tree over a hash index, and what an "index scan" executor
    // calls. Walks linked leaves after locating the start.
    std::vector<std::pair<KeyT, RID>> range(KeyT lo, KeyT hi) const {
        std::vector<std::pair<KeyT, RID>> out;
        Node* leaf = find_leaf(lo);
        int i = lower_bound_idx(leaf->keys, lo);
        while (leaf) {
            for (; i < (int)leaf->keys.size(); ++i) {
                if (leaf->keys[i] > hi) return out;
                out.emplace_back(leaf->keys[i], leaf->vals[i]);
            }
            leaf = leaf->next;
            i = 0;
        }
        return out;
    }

    int height() const {
        int h = 1; Node* n = root_;
        while (!n->leaf) { n = n->children[0]; ++h; }
        return h;
    }

private:
    struct Node {
        bool leaf;
        std::vector<KeyT> keys;
        // internal nodes use `children`; leaves use `vals` + `next`.
        std::vector<Node*> children;
        std::vector<RID>   vals;
        Node*  next   = nullptr;   // leaf-chain pointer (leaves only)
        Node*  parent = nullptr;
        explicit Node(bool is_leaf) : leaf(is_leaf) {}
    };

    int min_keys() const { return (order_ - 1) / 2; }

    // first index i with keys[i] >= key
    static int lower_bound_idx(const std::vector<KeyT>& ks, KeyT key) {
        return (int)(std::lower_bound(ks.begin(), ks.end(), key) - ks.begin());
    }

    // Descend from the root following separator keys to the responsible leaf.
    Node* find_leaf(KeyT key) const {
        Node* n = root_;
        while (!n->leaf) {
            // child index = first separator strictly greater than key
            int i = (int)(std::upper_bound(n->keys.begin(), n->keys.end(), key)
                          - n->keys.begin());
            n = n->children[i];
        }
        return n;
    }

    // ---- INSERT split -------------------------------------------------------
    void split_leaf(Node* leaf) {
        int mid = (int)leaf->keys.size() / 2;
        Node* sib = new Node(true);
        // right half moves to the new sibling
        sib->keys.assign(leaf->keys.begin() + mid, leaf->keys.end());
        sib->vals.assign(leaf->vals.begin() + mid, leaf->vals.end());
        leaf->keys.resize(mid);
        leaf->vals.resize(mid);
        // splice sibling into the leaf chain
        sib->next = leaf->next;
        leaf->next = sib;
        // the smallest key of the new leaf is COPIED up as the separator
        insert_into_parent(leaf, sib->keys.front(), sib);
    }

    void split_internal(Node* node) {
        int mid = (int)node->keys.size() / 2;
        KeyT up = node->keys[mid];          // middle key MOVES up (not copied)
        Node* sib = new Node(false);
        sib->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
        sib->children.assign(node->children.begin() + mid + 1, node->children.end());
        node->keys.resize(mid);
        node->children.resize(mid + 1);
        for (Node* c : sib->children) c->parent = sib;
        insert_into_parent(node, up, sib);
    }

    void insert_into_parent(Node* left, KeyT key, Node* right) {
        if (left == root_) {                // grew a new level
            Node* nr = new Node(false);
            nr->keys.push_back(key);
            nr->children.push_back(left);
            nr->children.push_back(right);
            left->parent = right->parent = nr;
            root_ = nr;
            return;
        }
        Node* p = left->parent;
        int i = (int)(std::upper_bound(p->keys.begin(), p->keys.end(), key)
                      - p->keys.begin());
        p->keys.insert(p->keys.begin() + i, key);
        p->children.insert(p->children.begin() + i + 1, right);
        right->parent = p;
        if ((int)p->keys.size() >= order_) split_internal(p);
    }

    // ---- DELETE rebalance (borrow or merge) --------------------------------
    void rebalance(Node* n) {
        if (n == root_) {
            // root may collapse a level once it has a single child
            if (!n->leaf && n->children.size() == 1) {
                root_ = n->children[0];
                root_->parent = nullptr;
                delete n;
            }
            return;
        }
        Node* p = n->parent;
        int idx = child_index(p, n);
        Node* left  = idx > 0 ? p->children[idx - 1] : nullptr;
        Node* right = idx + 1 < (int)p->children.size() ? p->children[idx + 1] : nullptr;

        // 1) try to BORROW one entry from a sibling with spare keys
        if (left && (int)left->keys.size() > min_keys())  { borrow_from_left(n, left, p, idx);  return; }
        if (right && (int)right->keys.size() > min_keys()) { borrow_from_right(n, right, p, idx); return; }

        // 2) otherwise MERGE with a sibling
        if (left)       merge(left, n, p, idx - 1);
        else if (right) merge(n, right, p, idx);

        if (p != root_ && (int)p->keys.size() < min_keys()) rebalance(p);
        else if (p == root_) rebalance(p);
    }

    static int child_index(Node* parent, Node* child) {
        for (int i = 0; i < (int)parent->children.size(); ++i)
            if (parent->children[i] == child) return i;
        return -1;
    }

    void borrow_from_left(Node* n, Node* left, Node* p, int idx) {
        if (n->leaf) {
            n->keys.insert(n->keys.begin(), left->keys.back());
            n->vals.insert(n->vals.begin(), left->vals.back());
            left->keys.pop_back(); left->vals.pop_back();
            p->keys[idx - 1] = n->keys.front();          // refresh separator
        } else {
            n->keys.insert(n->keys.begin(), p->keys[idx - 1]);
            p->keys[idx - 1] = left->keys.back();
            left->keys.pop_back();
            Node* moved = left->children.back();
            left->children.pop_back();
            n->children.insert(n->children.begin(), moved);
            moved->parent = n;
        }
    }

    void borrow_from_right(Node* n, Node* right, Node* p, int idx) {
        if (n->leaf) {
            n->keys.push_back(right->keys.front());
            n->vals.push_back(right->vals.front());
            right->keys.erase(right->keys.begin());
            right->vals.erase(right->vals.begin());
            p->keys[idx] = right->keys.front();          // refresh separator
        } else {
            n->keys.push_back(p->keys[idx]);
            p->keys[idx] = right->keys.front();
            right->keys.erase(right->keys.begin());
            Node* moved = right->children.front();
            right->children.erase(right->children.begin());
            n->children.push_back(moved);
            moved->parent = n;
        }
    }

    // Merge `right` into `left`, dropping separator at p->keys[sep].
    void merge(Node* left, Node* right, Node* p, int sep) {
        if (left->leaf) {
            left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
            left->vals.insert(left->vals.end(), right->vals.begin(), right->vals.end());
            left->next = right->next;
        } else {
            left->keys.push_back(p->keys[sep]);          // pull separator down
            left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
            for (Node* c : right->children) c->parent = left;
            left->children.insert(left->children.end(),
                                  right->children.begin(), right->children.end());
        }
        p->keys.erase(p->keys.begin() + sep);
        p->children.erase(p->children.begin() + sep + 1);
        delete right;
    }

    void destroy(Node* n) {
        if (!n) return;
        if (!n->leaf) for (Node* c : n->children) destroy(c);
        delete n;
    }

    Node* root_;
    int   order_;
};

}  // namespace minidb
