#ifndef BTREE_HPP
#define BTREE_HPP

#include <iostream>
#include <string>

class BTree;

class TreeNode {
private:
    int* data;
    TreeNode** children;
    int degree;
    int keyCount;
    bool isLeaf;

public:
    TreeNode(int degree, bool leaf);
    ~TreeNode();

    void inorderTraversal();
    TreeNode* searchKey(int value, int& pos);
    void insertInside(int value);
    void divideChild(int idx, TreeNode* node);
    void displayTree(const std::string& space);

    friend class BTree;
};

class BTree {
private:
    TreeNode* root;
    int minDegree;

public:
    BTree(int degree);
    ~BTree();

    void insert(int value);
    TreeNode* search(int value, int& position);
    void traverse();
    void printTree();

    bool empty() const {
        return root == nullptr;
    }
};

#endif
