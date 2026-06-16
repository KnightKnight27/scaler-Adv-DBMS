#ifndef REDBLACKTREE_H
#define REDBLACKTREE_H

#include <vector>
class RedBlackTree
{
public:
    enum Color
    {
        black = 0,
        red = 1
    };

    struct Node
    {
        int key;
        Node *leftChild;
        Node *rightChild;
        Node *parentNode;
        Color col;

        Node(int k)
            : key(k), leftChild(nullptr), rightChild(nullptr), parentNode(nullptr), col(Color::red)
        {
        }
    };

    void print();

    RedBlackTree();
    ~RedBlackTree();

    bool find(int val);
    void insert(int val);
    void remove(int val);

    Node *sentinel;

private:
    Node *root_;

    void fixTree(Node *node);

    // Tree manipulation helpers
    void leftRotate(Node *x);
    void rightRotate(Node *x);
    void deleteSubtree(Node *node);

    // Case checks
    bool isCase0(Node *node);
    bool isCase1(Node *node);
    bool isCase2(Node *node);
    bool isCase3(Node *node);

    void handleCase0(Node *node);
    void handleCase1(Node *node);
    void handleCase2(Node *node);
    void handleCase3(Node *node);

    Node *getGrandParent(Node *node);
    Node *getUncle(Node *node);
};

#endif