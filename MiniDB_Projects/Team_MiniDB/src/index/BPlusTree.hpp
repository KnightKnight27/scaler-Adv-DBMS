#pragma once

#include <optional>
#include <vector>

using namespace std;

#include "common/Config.hpp"
#include "common/Types.hpp"

namespace minidb {

struct BPlusTreeNode {
    bool is_leaf = true;
    vector<int> keys;
    vector<BPlusTreeNode*> children;
    vector<RowLocation> locations;
    BPlusTreeNode* next = nullptr;
};

class BPlusTree {
public:
    explicit BPlusTree(int order = BTREE_ORDER);
    ~BPlusTree();

    optional<RowLocation> search(int key) const;
    void insert(int key, const RowLocation& loc);
    bool remove(int key);
    vector<int> getAllKeys() const;

private:
    int order_;
    BPlusTreeNode* root_;

    void destroy(BPlusTreeNode* node);
    BPlusTreeNode* findLeaf(int key) const;
    void insertIntoLeaf(BPlusTreeNode* leaf, int key, const RowLocation& loc);
    void insertIntoParent(BPlusTreeNode* left, int key, BPlusTreeNode* right);
    bool removeFromLeaf(BPlusTreeNode* leaf, int key);
};

}  // namespace minidb
