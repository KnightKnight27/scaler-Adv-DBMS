#pragma once
#include <vector>
#include <string>
#include <optional>
#include <iostream>

const int MAX_KEYS = 4;

struct Node {
    bool isLeaf;
    std::vector<int> keys;
    std::vector<std::string> values;  // leaf nodes only
    std::vector<Node*> children;      // internal nodes only
    Node* next;                        // leaf linked list (leaf nodes only)
    Node* parent;

    explicit Node(bool leaf);
};

class BPlusTree {
public:
    BPlusTree();
    ~BPlusTree();

    void insert(int key, const std::string& value);
    std::optional<std::string> search(int key) const;
    std::vector<std::pair<int, std::string>> range_scan(int lo, int hi) const;
    void delete_key(int key);
    void print_tree() const;

private:
    Node* root;

    Node* find_leaf(int key) const;
    void insert_into_leaf(Node* leaf, int key, const std::string& value);
    void split_leaf(Node* leaf);
    void split_internal(Node* node);
    void insert_into_parent(Node* left, int push_up_key, Node* right);

    void delete_from_leaf(Node* leaf, int key);
    void fix_underflow(Node* node);
    bool borrow_from_left_sibling(Node* node, Node* sibling, Node* parent, int separator_idx);
    bool borrow_from_right_sibling(Node* node, Node* sibling, Node* parent, int separator_idx);
    void merge_nodes(Node* left, Node* right, Node* parent, int separator_idx);

    void destroy_tree(Node* node);
    void print_level(Node* node, int depth) const;
};
