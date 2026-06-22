#include "bplus_tree.hpp"
#include <algorithm>

namespace minidb {

void BPlusTree::destroy(Node* n) {
    if (!n) return;
    if (!n->leaf)
        for (Node* c : n->children) destroy(c);
    delete n;
}

// Descend from the root choosing, at each internal node, the child whose
// subtree could hold `key`: advance while key >= separator[i], then descend.
BPlusTree::Node* BPlusTree::findLeaf(int64_t key) const {
    Node* cur = root_;
    while (cur && !cur->leaf) {
        size_t i = 0;
        while (i < cur->keys.size() && key >= cur->keys[i]) i++;
        cur = cur->children[i];
    }
    return cur;
}

bool BPlusTree::search(int64_t key, RowId& out) const {
    Node* leaf = findLeaf(key);
    if (!leaf) return false;
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    if (it != leaf->keys.end() && *it == key) {
        out = leaf->vals[it - leaf->keys.begin()];
        return true;
    }
    return false;
}

void BPlusTree::insert(int64_t key, RowId rid) {
    if (!root_) {
        root_ = new Node(/*leaf=*/true);
        root_->keys.push_back(key);
        root_->vals.push_back(rid);
        count_ = 1;
        return;
    }
    bool split = false;
    int64_t up_key = 0;
    Node* new_right = nullptr;
    insertInto(root_, key, rid, split, up_key, new_right);
    if (split) {
        // Root split: build a new root one level taller.
        Node* new_root = new Node(/*leaf=*/false);
        new_root->keys.push_back(up_key);
        new_root->children.push_back(root_);
        new_root->children.push_back(new_right);
        root_ = new_root;
    }
}

void BPlusTree::insertInto(Node* node, int64_t key, RowId rid,
                           bool& split, int64_t& up_key, Node*& new_right) {
    split = false;
    if (node->leaf) {
        auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
        size_t pos = it - node->keys.begin();
        if (it != node->keys.end() && *it == key) {
            node->vals[pos] = rid; // upsert: duplicate key overwrites
            return;
        }
        node->keys.insert(node->keys.begin() + pos, key);
        node->vals.insert(node->vals.begin() + pos, rid);
        count_++;
        if (static_cast<int>(node->keys.size()) < ORDER) return;

        // Split the leaf: right half moves to a new sibling; first key of the
        // right half is copied up as the separator.
        Node* right = new Node(/*leaf=*/true);
        size_t mid = node->keys.size() / 2;
        right->keys.assign(node->keys.begin() + mid, node->keys.end());
        right->vals.assign(node->vals.begin() + mid, node->vals.end());
        node->keys.resize(mid);
        node->vals.resize(mid);
        right->next = node->next;
        node->next = right;
        up_key = right->keys.front();
        new_right = right;
        split = true;
        return;
    }

    // Internal node: route to the right child, then handle a child split.
    size_t i = 0;
    while (i < node->keys.size() && key >= node->keys[i]) i++;
    bool child_split = false;
    int64_t child_up = 0;
    Node* child_right = nullptr;
    insertInto(node->children[i], key, rid, child_split, child_up, child_right);
    if (!child_split) return;

    node->keys.insert(node->keys.begin() + i, child_up);
    node->children.insert(node->children.begin() + i + 1, child_right);
    if (static_cast<int>(node->children.size()) <= ORDER) return;

    // Split the internal node: the middle separator is pushed up (not copied).
    Node* right = new Node(/*leaf=*/false);
    size_t mid = node->keys.size() / 2;
    up_key = node->keys[mid];
    right->keys.assign(node->keys.begin() + mid + 1, node->keys.end());
    right->children.assign(node->children.begin() + mid + 1, node->children.end());
    node->keys.resize(mid);
    node->children.resize(mid + 1);
    new_right = right;
    split = true;
}

void BPlusTree::erase(int64_t key) {
    Node* leaf = findLeaf(key);
    if (!leaf) return;
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    if (it != leaf->keys.end() && *it == key) {
        size_t pos = it - leaf->keys.begin();
        leaf->keys.erase(leaf->keys.begin() + pos);
        leaf->vals.erase(leaf->vals.begin() + pos);
        count_--;
    }
    // Leaves are not merged/rebalanced on delete (lazy deletion). Search and
    // range scan stay correct; only fill factor degrades. Documented trade-off.
}

void BPlusTree::rangeScan(int64_t lo, int64_t hi,
                          const std::function<void(int64_t, RowId)>& fn) const {
    Node* leaf = findLeaf(lo);
    while (leaf) {
        for (size_t i = 0; i < leaf->keys.size(); i++) {
            if (leaf->keys[i] < lo) continue;
            if (leaf->keys[i] > hi) return;
            fn(leaf->keys[i], leaf->vals[i]);
        }
        leaf = leaf->next;
    }
}

} // namespace minidb
