#ifndef RED_BLACK_TREE_H
#define RED_BLACK_TREE_H

enum Color { RED, BLACK };

struct Node {
    int data;
    Color color;
    Node* left;
    Node* right;
    Node* parent;

    Node(int val);
};

class RedBlackTree {
private:
    Node* root;

    void leftRotate(Node* x);
    void rightRotate(Node* x);
    void fixInsert(Node* k);
    void inorderHelper(Node* root);
    void destroyTree(Node* node);

public:
    RedBlackTree();
    ~RedBlackTree();

    void insert(int key);
    bool search(int key);
    void printInOrder();
};

#endif
