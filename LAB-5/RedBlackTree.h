// Lab 5 - Red-Black Tree (header)
// Bhavya Jain (23BCS10088) <Bhavya.23bcs10088@sst.scaler.com>
//
// A standards-conformant Red-Black Tree (Cormen / CLRS variant) using a
// single shared sentinel for nil leaves. Supports insert, find, erase, and
// an in-order traversal. All invariants are preserved on every mutation:
//   1. Every node is RED or BLACK.
//   2. The root is BLACK.
//   3. Every leaf (sentinel) is BLACK.
//   4. A RED node has only BLACK children.
//   5. Every root-to-leaf path crosses the same number of BLACK nodes.

#ifndef REDBLACKTREE_H
#define REDBLACKTREE_H

#include <cstddef>
#include <vector>

class RedBlackTree {
public:
    enum Color { BLACK = 0, RED = 1 };

    struct Node {
        int   key;
        Color col;
        Node* left;
        Node* right;
        Node* parent;
    };

    RedBlackTree();
    ~RedBlackTree();

    RedBlackTree(const RedBlackTree&)            = delete;
    RedBlackTree& operator=(const RedBlackTree&) = delete;

    bool   find(int key) const;
    void   insert(int key);
    bool   erase(int key);
    size_t size() const { return size_; }
    bool   empty() const { return size_ == 0; }

    // BFS level-order print, LeetCode-style ("[1, 2, null, 3]").
    void print() const;

    // In-order traversal — sorted output, useful for verification.
    std::vector<int> inorder() const;

    // Structural invariant check (returns false if any RB property fails).
    bool validate() const;

private:
    Node* root_;
    Node* nil_;
    size_t size_;

    Node* newNode(int key, Color col, Node* parent);

    void leftRotate(Node* x);
    void rightRotate(Node* x);

    void insertFixup(Node* z);

    void transplant(Node* u, Node* v);
    Node* minimum(Node* x) const;
    Node* findNode(int key) const;
    void  eraseFixup(Node* x);

    void destroy(Node* n);

    void inorderInto(Node* n, std::vector<int>& out) const;

    // Validation helpers
    int  blackHeight(Node* n, bool& ok) const;
};

#endif
