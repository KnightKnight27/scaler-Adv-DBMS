#ifndef BPLUS_TREE_H
#define BPLUS_TREE_H

#include <vector>
#include <utility>

struct BPlusNode {
    bool is_leaf;
    std::vector<int> keys;
    // For leaf nodes: leaf_values[i] corresponds to keys[i]
    std::vector<std::pair<int, int>> leaf_values;
    // For internal nodes: children[i] points to BPlusNode subtree
    std::vector<BPlusNode*> children;
    BPlusNode* next{nullptr}; // next leaf node pointer

    explicit BPlusNode(bool is_leaf) : is_leaf(is_leaf) {}
    ~BPlusNode();
};

class BPlusTree {
private:
    BPlusNode* root;
    int order;

    void insert_non_full(BPlusNode* node, int key, std::pair<int, int> value);
    void split_child(BPlusNode* parent, int idx);
    bool delete_helper(BPlusNode* node, int key);
    void handle_underflow(BPlusNode* parent, int idx);

public:
    explicit BPlusTree(int order = 4);
    ~BPlusTree();

    bool search(int key, std::pair<int, int>& value) const;
    std::vector<std::pair<int, std::pair<int, int>>> range_search(int start_key, int end_key) const;
    bool insert(int key, std::pair<int, int> value);
    bool delete_key(int key);
};

#endif
