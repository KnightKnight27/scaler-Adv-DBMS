#include "bplustree.h"
#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace minidb {

BPlusTree::BPlusTree(int order) : order_(order) {
    root_ = new Node(true);  // start with a single empty leaf
}

BPlusTree::~BPlusTree() { destroy(root_); }

void BPlusTree::destroy(Node* n) {
    if (!n) return;
    if (!n->leaf) for (Node* c : n->children) destroy(c);
    delete n;
}

// ── Search ────────────────────────────────────────────────────────────────────

BPlusTree::Node* BPlusTree::find_leaf(const Value& key) const {
    Node* cur = root_;
    while (!cur->leaf) {
        // upper_bound gives the first key > search key.
        // The child to the left of that key is the right subtree.
        auto it = std::upper_bound(cur->keys.begin(), cur->keys.end(), key);
        int  i  = static_cast<int>(it - cur->keys.begin());
        cur = cur->children[i];
    }
    return cur;
}

bool BPlusTree::search(const Value& key, RID& out) const {
    Node* leaf = find_leaf(key);
    auto  it   = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    if (it == leaf->keys.end() || *it != key) return false;
    int idx = static_cast<int>(it - leaf->keys.begin());
    out = leaf->rids[idx];
    return true;
}

// ── Range scan ────────────────────────────────────────────────────────────────

std::vector<RID> BPlusTree::range(const Value& lo, const Value& hi) const {
    std::vector<RID> result;
    Node* leaf = find_leaf(lo);
    // Walk the linked leaf chain collecting all keys in [lo, hi].
    while (leaf) {
        for (int i = 0; i < static_cast<int>(leaf->keys.size()); i++) {
            if (leaf->keys[i] < lo)  continue;
            if (leaf->keys[i] > hi)  return result;
            result.push_back(leaf->rids[i]);
        }
        leaf = leaf->next;
    }
    return result;
}

// ── Insert ────────────────────────────────────────────────────────────────────
// insert_rec descends the tree and inserts at the leaf.  When a node overflows
// it is split in two, and the median separator key is propagated upward.
// Returns true if a split occurred (caller must insert the new child).

bool BPlusTree::insert_rec(BPlusTree::Node* node, const Value& key, RID rid,
                            Value& out_key, BPlusTree::Node*& out_child) {
    if (node->leaf) {
        // Insert the key in sorted order into the leaf.
        auto pos = std::lower_bound(node->keys.begin(), node->keys.end(), key);
        int  idx = static_cast<int>(pos - node->keys.begin());
        node->keys.insert(pos, key);
        node->rids.insert(node->rids.begin() + idx, rid);
        count_++;

        if (static_cast<int>(node->keys.size()) <= order_)
            return false;  // no split needed

        // Leaf overflow: split into two halves.
        // The left node keeps keys[0..mid-1]; the right node gets keys[mid..].
        int   mid   = static_cast<int>(node->keys.size()) / 2;
        Node* right = new Node(true);

        right->keys.assign(node->keys.begin() + mid, node->keys.end());
        right->rids.assign(node->rids.begin() + mid, node->rids.end());
        node->keys.resize(mid);
        node->rids.resize(mid);

        // Thread the leaf chain.
        right->next = node->next;
        node->next  = right;

        // The separator pushed to the parent is the first key of the right leaf.
        out_key   = right->keys[0];
        out_child = right;
        return true;
    }

    // Internal node: descend to the right child.
    auto pos = std::upper_bound(node->keys.begin(), node->keys.end(), key);
    int  idx = static_cast<int>(pos - node->keys.begin());

    Value new_key; Node* new_child = nullptr;
    bool split = insert_rec(node->children[idx], key, rid, new_key, new_child);

    if (!split) return false;

    // A child split: insert the promoted key and new child pointer here.
    node->keys.insert(node->keys.begin() + idx, new_key);
    node->children.insert(node->children.begin() + idx + 1, new_child);

    if (static_cast<int>(node->keys.size()) <= order_)
        return false;  // this internal node is still within capacity

    // Internal node overflow: split it too.
    int   mid   = static_cast<int>(node->keys.size()) / 2;
    Node* right = new Node(false);

    // The median key is promoted; it does NOT stay in either half.
    out_key = node->keys[mid];

    right->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
    right->children.assign(node->children.begin() + mid + 1,
                            node->children.end());

    node->keys.resize(mid);
    node->children.resize(mid + 1);

    out_child = right;
    return true;
}

void BPlusTree::insert(const Value& key, RID rid) {
    Value new_key; Node* new_child = nullptr;
    bool  split = insert_rec(root_, key, rid, new_key, new_child);

    if (split) {
        // Root was split: create a new root with two children.
        Node* new_root        = new Node(false);
        new_root->keys        = {new_key};
        new_root->children    = {root_, new_child};
        root_                 = new_root;
    }
}

// ── Delete (lazy) ─────────────────────────────────────────────────────────────
// We only remove the key from the leaf without rebalancing.
// This can lead to under-full nodes over time, which is noted as a limitation.

bool BPlusTree::erase(const Value& key) {
    Node* leaf = find_leaf(key);
    auto  it   = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    if (it == leaf->keys.end() || *it != key) return false;

    int idx = static_cast<int>(it - leaf->keys.begin());
    leaf->keys.erase(leaf->keys.begin() + idx);
    leaf->rids.erase(leaf->rids.begin() + idx);
    count_--;
    return true;
}

// ── Height ────────────────────────────────────────────────────────────────────

int BPlusTree::height() const {
    int h = 1;
    Node* cur = root_;
    while (!cur->leaf) { h++; cur = cur->children[0]; }
    return h;
}

} // namespace minidb
