#ifndef RBTREE_H
#define RBTREE_H

#include <string>

class RBTree {
public:
    enum Shade {
        RED,
        BLACK
    };

    struct TreeNode {
        int key;
        Shade shade;

        TreeNode* leftChild;
        TreeNode* rightChild;
        TreeNode* parentNode;

        TreeNode(int value, TreeNode* leaf)
            : key(value),
              shade(RED),
              leftChild(leaf),
              rightChild(leaf),
              parentNode(nullptr) {}
    };

    RBTree();
    ~RBTree();

    void insertValue(int value);
    bool contains(int value) const;
    void display() const;

private:
    TreeNode* rootNode;
    TreeNode* sentinel;

    void balanceInsertion(TreeNode* node);

    void leftShift(TreeNode* node);
    void rightShift(TreeNode* node);

    void destroy(TreeNode* node);

    bool nodeIsRed(TreeNode* node) const {
        return node != sentinel && node->shade == RED;
    }
};

#endif