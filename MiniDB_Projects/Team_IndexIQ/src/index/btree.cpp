#include "index/btree.h"
#include <algorithm>

BTree::BTree() : root_(nullptr) {}

BTree::~BTree() { delete root_; }

BNode* BTree::find_leaf(int key) const {
    BNode* node = root_;
    while (!node->is_leaf) {
        int i = 0;
        while (i < (int)node->keys.size() && key >= node->keys[i]) i++;
        node = node->children[i];
    }
    return node;
}

std::optional<int> BTree::search(int key) const {
    if (!root_) return std::nullopt;
    BNode* leaf = find_leaf(key);
    for (int i = 0; i < (int)leaf->keys.size(); i++)
        if (leaf->keys[i] == key) return leaf->vals[i];
    return std::nullopt;
}

std::pair<int,BNode*> BTree::split_leaf(BNode* leaf) {
    int mid = ((int)leaf->keys.size() + 1) / 2;
    BNode* right = new BNode(true);
    right->keys = std::vector<int>(leaf->keys.begin() + mid, leaf->keys.end());
    right->vals = std::vector<int>(leaf->vals.begin() + mid, leaf->vals.end());
    leaf->keys.resize(mid);
    leaf->vals.resize(mid);
    right->next = leaf->next;
    leaf->next  = right;
    return {right->keys[0], right};
}

std::pair<int,BNode*> BTree::split_internal(BNode* node) {
    int mid    = (int)node->keys.size() / 2;
    int up_key = node->keys[mid];
    BNode* right = new BNode(false);
    right->keys     = std::vector<int>(node->keys.begin() + mid + 1, node->keys.end());
    right->children = std::vector<BNode*>(node->children.begin() + mid + 1,
                                          node->children.end());
    node->keys.resize(mid);
    node->children.resize(mid + 1);
    return {up_key, right};
}

std::optional<std::pair<int,BNode*>> BTree::insert_rec(BNode* node, int key, int row_id) {
    if (node->is_leaf) {
        auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
        int  idx = (int)(it - node->keys.begin());
        node->keys.insert(it, key);
        node->vals.insert(node->vals.begin() + idx, row_id);

        if ((int)node->keys.size() <= BTREE_MAX_KEYS) return std::nullopt;
        return split_leaf(node);
    }

    int i = 0;
    while (i < (int)node->keys.size() && key >= node->keys[i]) i++;

    auto result = insert_rec(node->children[i], key, row_id);
    if (!result) return std::nullopt;

    auto [up_key, new_right] = *result;
    node->keys.insert(node->keys.begin() + i, up_key);
    node->children.insert(node->children.begin() + i + 1, new_right);

    if ((int)node->keys.size() <= BTREE_MAX_KEYS) return std::nullopt;
    return split_internal(node);
}

void BTree::insert(int key, int row_id) {
    if (!root_) {
        root_ = new BNode(true);
    }
    auto result = insert_rec(root_, key, row_id);
    if (!result) return;

    auto [up_key, new_right] = *result;
    BNode* new_root     = new BNode(false);
    new_root->keys      = {up_key};
    new_root->children  = {root_, new_right};
    root_               = new_root;
}

void BTree::remove(int key) {
    if (!root_) return;
    BNode* leaf = find_leaf(key);
    for (int i = 0; i < (int)leaf->keys.size(); i++) {
        if (leaf->keys[i] == key) {
            leaf->keys.erase(leaf->keys.begin() + i);
            leaf->vals.erase(leaf->vals.begin() + i);
            return;
        }
    }
}

void BTree::range_scan(int lo, int hi, std::function<void(int,int)> cb) const {
    if (!root_) return;
    BNode* leaf = find_leaf(lo);
    while (leaf) {
        for (int i = 0; i < (int)leaf->keys.size(); i++) {
            if (leaf->keys[i] > hi) return;
            if (leaf->keys[i] >= lo) cb(leaf->keys[i], leaf->vals[i]);
        }
        leaf = leaf->next;
    }
}
