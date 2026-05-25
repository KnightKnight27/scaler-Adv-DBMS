#pragma once

enum Color { RED, BLACK };

struct Node {
    int   data;
    Color color;
    Node* left;
    Node* right;
    Node* parent;

    explicit Node(int val);
};

class RedBlackTree {
public:
    RedBlackTree();
    ~RedBlackTree();

    void insert(int val);

    void remove(int val);

    bool search(int val) const;

    void printInorder() const;

    void printLevels() const;

    bool isValid() const;

private:
    Node* root;

    void rotateLeft(Node* x);
    void rotateRight(Node* y);

    void fixInsert(Node* z);
    void fixDelete(Node* x, Node* xParent);

    void transplant(Node* u, Node* v);
    Node* minimum(Node* node) const;

    void inorder(Node* node) const;
    void destroyTree(Node* node);

    int  blackHeight(Node* node) const;
    bool noConsecutiveRed(Node* node) const;
};
