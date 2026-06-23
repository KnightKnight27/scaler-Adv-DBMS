#include "index/BPlusTree.hpp"

using namespace std;

namespace minidb {

BPlusTree::BPlusTree(int order) : order_(order), root_(nullptr) {
    root_ = new BPlusTreeNode();
    root_->is_leaf = true;
}

BPlusTree::~BPlusTree() { destroy(root_); }

void BPlusTree::destroy(BPlusTreeNode* node) {
    if (!node) return;
    if (!node->is_leaf) for (auto* c : node->children) destroy(c);
    delete node;
}

BPlusTreeNode* BPlusTree::findLeaf(int key) const {
    BPlusTreeNode* node = root_;
    while (!node->is_leaf) {
        int i = 0;
        while (i < (int)node->keys.size() && key >= node->keys[i]) ++i;
        node = node->children[i];
    }
    return node;
}

optional<RowLocation> BPlusTree::search(int key) const {
    BPlusTreeNode* leaf = findLeaf(key);
    for (size_t i = 0; i < leaf->keys.size(); ++i)
        if (leaf->keys[i] == key) return leaf->locations[i];
    return nullopt;
}

void BPlusTree::insertIntoLeaf(BPlusTreeNode* leaf, int key, const RowLocation& loc) {
    size_t pos = 0;
    while (pos < leaf->keys.size() && leaf->keys[pos] < key) ++pos;
    leaf->keys.insert(leaf->keys.begin() + pos, key);
    leaf->locations.insert(leaf->locations.begin() + pos, loc);
}

void BPlusTree::insertIntoParent(BPlusTreeNode* left, int key, BPlusTreeNode* right) {
    BPlusTreeNode* new_root = new BPlusTreeNode();
    new_root->is_leaf = false;
    new_root->keys.push_back(key);
    new_root->children.push_back(left);
    new_root->children.push_back(right);
    root_ = new_root;
}

void BPlusTree::insert(int key, const RowLocation& loc) {
    if (search(key).has_value()) return;
    BPlusTreeNode* leaf = findLeaf(key);
    insertIntoLeaf(leaf, key, loc);
    if ((int)leaf->keys.size() >= order_) {
        BPlusTreeNode* new_leaf = new BPlusTreeNode();
        new_leaf->is_leaf = true;
        int mid = (int)leaf->keys.size() / 2;
        new_leaf->keys.assign(leaf->keys.begin() + mid, leaf->keys.end());
        new_leaf->locations.assign(leaf->locations.begin() + mid, leaf->locations.end());
        leaf->keys.resize(mid);
        leaf->locations.resize(mid);
        new_leaf->next = leaf->next;
        leaf->next = new_leaf;
        insertIntoParent(leaf, new_leaf->keys[0], new_leaf);
    }
}

bool BPlusTree::removeFromLeaf(BPlusTreeNode* leaf, int key) {
    for (size_t i = 0; i < leaf->keys.size(); ++i) {
        if (leaf->keys[i] == key) {
            leaf->keys.erase(leaf->keys.begin() + i);
            leaf->locations.erase(leaf->locations.begin() + i);
            return true;
        }
    }
    return false;
}

bool BPlusTree::remove(int key) { return removeFromLeaf(findLeaf(key), key); }

vector<int> BPlusTree::getAllKeys() const {
    vector<int> keys;
    BPlusTreeNode* node = root_;
    while (!node->is_leaf) node = node->children[0];
    while (node) {
        keys.insert(keys.end(), node->keys.begin(), node->keys.end());
        node = node->next;
    }
    return keys;
}

}  // namespace minidb
