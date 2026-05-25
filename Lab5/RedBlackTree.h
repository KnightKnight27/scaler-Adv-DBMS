#ifndef REDBLACKTREE_H
#define REDBLACKTREE_H

class RedBlackTree {
public:
    enum class Color { Red, Black };

    struct Node {
        int data;
        Color color;
        Node *left, *right, *parent;

        Node(int d, Node *nil)
            : data(d), color(Color::Red), left(nil), right(nil), parent(nullptr) {}
    };

    RedBlackTree();
    ~RedBlackTree();

    void insert(int val);
    bool search(int val);
    void print();

private:
    Node *root;
    Node *nil; // sentinel leaf

    void fixInsert(Node *node);
    void rotateLeft(Node *pivot);
    void rotateRight(Node *pivot);
    void freeTree(Node *node);

    bool isRed(Node *node) { return node != nil && node->color == Color::Red; }
};

#endif
