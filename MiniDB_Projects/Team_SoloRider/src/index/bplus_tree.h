#pragma once
#include "common/types.h"
#include <vector>

namespace minidb {

struct BPlusNode {
    bool is_leaf;
    std::vector<int> keys;
    std::vector<BPlusNode*> children;
    std::vector<RecordId> values;
    BPlusNode* next = nullptr;
    BPlusNode* parent = nullptr;
};

class BPlusTree {
public:
    explicit BPlusTree(int order = 4);
    ~BPlusTree();

    RecordId search(int key);
    bool insert(int key, RecordId rid);
    bool remove(int key);
    std::vector<RecordId> range_scan(int low_key, int high_key);
    void print_tree();
    int get_height();
    bool is_empty() const;

private:
    BPlusNode* root_ = nullptr;
    int order_;

    BPlusNode* find_leaf(int key);
    void insert_into_leaf(BPlusNode* leaf, int key, RecordId rid);
    void insert_into_parent(BPlusNode* old_node, int key, BPlusNode* new_node);
    void split_leaf(BPlusNode* leaf);
    void split_internal(BPlusNode* node);
    void remove_from_leaf(BPlusNode* leaf, int key);
    void handle_underflow(BPlusNode* node);
    int find_sibling_index(BPlusNode* parent, BPlusNode* child);
    void delete_tree(BPlusNode* node);
};

} // namespace minidb
