// Lab 5 - Red-Black Tree (header)
//
// CLRS-style red-black tree over int keys. A single shared `nil_` sentinel
// stands in for every leaf so insert/delete fixups never special-case
// nullptr. Public surface mirrors the typical RBT API: insert, find,
// remove, print (level-order BFS).

#ifndef LAB5_RED_BLACK_TREE_H_
#define LAB5_RED_BLACK_TREE_H_

#include <vector>

enum class Color { RED, BLACK };

struct Node {
    int   key;
    Color color;
    Node* left;
    Node* right;
    Node* parent;
};

class RedBlackTree {
public:
    RedBlackTree();
    ~RedBlackTree();

    RedBlackTree(const RedBlackTree&)            = delete;
    RedBlackTree& operator=(const RedBlackTree&) = delete;

    void insert(int key);
    bool find(int key) const;
    bool remove(int key);

    // Level-order traversal, returned as a vector of (key, color, depth)
    // tuples so callers can render the tree however they want. `print`
    // dumps a readable form to stdout.
    void print() const;

    // Verifies the five RB invariants and returns the black-height. Aborts
    // via assert() on violation. Useful as a self-check after mutations.
    int  checkInvariants() const;

private:
    Node* nil_;
    Node* root_;

    Node* findNode(int key) const;
    Node* minimum(Node* x) const;

    void  leftRotate(Node* x);
    void  rightRotate(Node* x);

    void  insertFixup(Node* z);
    void  transplant(Node* u, Node* v);
    void  deleteFixup(Node* x);

    void  destroy(Node* x);
    int   blackHeightOf(Node* x) const;
};

#endif  // LAB5_RED_BLACK_TREE_H_
