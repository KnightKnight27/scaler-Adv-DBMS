#ifndef REDBLACKTREE_H
#define REDBLACKTREE_H
#include <vector>

class RedBlackTree {
public:
    enum Color {
        BLACK = 0,
        RED = 1
    };

    struct Node {
        int data;
        Node *leftChild;
        Node *rightChild;
        Node *parentNode;
        Color nodeColor;

        Node(int value)
            : data(value), leftChild(nullptr), rightChild(nullptr),
              parentNode(nullptr), nodeColor(Color::RED)
        {}
    };

    RedBlackTree();
    ~RedBlackTree();

    bool search(int value);
    void insert(int value);
    void deleteNode(int value);
    void display();

    Node *SENTINEL;

private:
    Node *root;

    void rebalance(Node *current);

    bool checkCase0(Node *current);
    bool checkCase1(Node *current);
    bool checkCase2(Node *current);
    bool checkCase3(Node *current);

    void resolveCase0(Node *current);
    void resolveCase1(Node *current);
    void resolveCase2(Node *current);
    void resolveCase3(Node *current);

    Node *fetchGrandParent(Node *current);
    Node *fetchUncle(Node *current);
};

#endif