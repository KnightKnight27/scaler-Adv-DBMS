#pragma once

#include <iostream>
#include <string>
#include <algorithm>

// ─── Color Enum ──────────────────────────────────────────────────────────────

enum Color { RED, BLACK };

// ─── Node ────────────────────────────────────────────────────────────────────

struct Node {
    int   key;
    Color color;
    Node* left;
    Node* right;
    Node* parent;

    Node(int k, Color c, Node* nil)
        : key(k), color(c), left(nil), right(nil), parent(nil) {}
};

// ─── Red-Black Tree ──────────────────────────────────────────────────────────

class RedBlackTree {
public:
    RedBlackTree() {
        nil_        = new Node(0, BLACK, nullptr);
        nil_->left  = nil_;
        nil_->right = nil_;
        nil_->parent= nil_;
        root_       = nil_;
    }

    ~RedBlackTree() {
        destroyTree(root_);
        delete nil_;
    }

    // ── Public interface ─────────────────────────────────────────────────────

    void insert(int key) {
        Node* z   = new Node(key, RED, nil_);
        Node* y   = nil_;
        Node* x   = root_;

        while (x != nil_) {
            y = x;
            if      (z->key < x->key) x = x->left;
            else if (z->key > x->key) x = x->right;
            else { delete z; return; }          // duplicate — ignore
        }

        z->parent = y;
        if      (y == nil_)         root_    = z;
        else if (z->key < y->key)   y->left  = z;
        else                        y->right = z;

        insertFixup(z);
    }

    void remove(int key) {
        Node* z = search(key);
        if (z == nil_) return;
        deleteNode(z);
    }

    Node* search(int key) const {
        Node* x = root_;
        while (x != nil_) {
            if      (key < x->key) x = x->left;
            else if (key > x->key) x = x->right;
            else                   return x;
        }
        return nil_;
    }

    Node* getNil() const { return nil_; }

    // ── Traversals ───────────────────────────────────────────────────────────

    void inorder()   const { inorderHelper(root_);   std::cout << "\n"; }
    void preorder()  const { preorderHelper(root_);  std::cout << "\n"; }
    void postorder() const { postorderHelper(root_); std::cout << "\n"; }

    // ── Visual print ─────────────────────────────────────────────────────────

    void printTree() const {
        if (root_ == nil_) {
            std::cout << "(empty tree)\n";
            return;
        }
        printHelper(root_, "", true);
    }

    // ── Validation ───────────────────────────────────────────────────────────

    bool validateProperties() const {
        // Rule 1: root is BLACK
        if (root_->color != BLACK) return false;

        // Rule 2: nil sentinel is BLACK (always true by construction)
        // Rule 3–5 checked recursively
        int bh = 0;
        return validateHelper(root_, bh);
    }

private:
    Node* root_;
    Node* nil_;

    // ── Rotations ────────────────────────────────────────────────────────────

    void leftRotate(Node* x) {
        Node* y  = x->right;
        x->right = y->left;
        if (y->left != nil_) y->left->parent = x;
        y->parent = x->parent;
        if      (x->parent == nil_)     root_           = y;
        else if (x == x->parent->left)  x->parent->left = y;
        else                            x->parent->right= y;
        y->left   = x;
        x->parent = y;
    }

    void rightRotate(Node* x) {
        Node* y  = x->left;
        x->left  = y->right;
        if (y->right != nil_) y->right->parent = x;
        y->parent = x->parent;
        if      (x->parent == nil_)     root_            = y;
        else if (x == x->parent->right) x->parent->right = y;
        else                            x->parent->left  = y;
        y->right  = x;
        x->parent = y;
    }

    // ── Insert fixup ─────────────────────────────────────────────────────────

    void insertFixup(Node* z) {
        while (z->parent->color == RED) {
            if (z->parent == z->parent->parent->left) {
                Node* y = z->parent->parent->right;        // uncle
                if (y->color == RED) {                      // Case 1
                    z->parent->color          = BLACK;
                    y->color                  = BLACK;
                    z->parent->parent->color  = RED;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->right) {            // Case 2
                        z = z->parent;
                        leftRotate(z);
                    }
                    z->parent->color         = BLACK;       // Case 3
                    z->parent->parent->color = RED;
                    rightRotate(z->parent->parent);
                }
            } else {
                Node* y = z->parent->parent->left;          // uncle (mirror)
                if (y->color == RED) {                      // Case 1
                    z->parent->color         = BLACK;
                    y->color                 = BLACK;
                    z->parent->parent->color = RED;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->left) {             // Case 2
                        z = z->parent;
                        rightRotate(z);
                    }
                    z->parent->color         = BLACK;       // Case 3
                    z->parent->parent->color = RED;
                    leftRotate(z->parent->parent);
                }
            }
        }
        root_->color = BLACK;
    }

    // ── Delete helpers ───────────────────────────────────────────────────────

    Node* minimum(Node* x) const {
        while (x->left != nil_) x = x->left;
        return x;
    }

    void transplant(Node* u, Node* v) {
        if      (u->parent == nil_)       root_            = v;
        else if (u == u->parent->left)    u->parent->left  = v;
        else                              u->parent->right = v;
        v->parent = u->parent;
    }

    void deleteNode(Node* z) {
        Node* y        = z;
        Node* x        = nil_;
        Color origColor = y->color;

        if (z->left == nil_) {
            x = z->right;
            transplant(z, z->right);
        } else if (z->right == nil_) {
            x = z->left;
            transplant(z, z->left);
        } else {
            y        = minimum(z->right);
            origColor = y->color;
            x        = y->right;
            if (y->parent == z) {
                x->parent = y;
            } else {
                transplant(y, y->right);
                y->right         = z->right;
                y->right->parent = y;
            }
            transplant(z, y);
            y->left         = z->left;
            y->left->parent = y;
            y->color        = z->color;
        }
        delete z;
        if (origColor == BLACK) deleteFixup(x);
    }

    void deleteFixup(Node* x) {
        while (x != root_ && x->color == BLACK) {
            if (x == x->parent->left) {
                Node* w = x->parent->right;
                if (w->color == RED) {                          // Case 1
                    w->color          = BLACK;
                    x->parent->color  = RED;
                    leftRotate(x->parent);
                    w = x->parent->right;
                }
                if (w->left->color == BLACK && w->right->color == BLACK) {
                    w->color = RED;                             // Case 2
                    x = x->parent;
                } else {
                    if (w->right->color == BLACK) {             // Case 3
                        w->left->color = BLACK;
                        w->color       = RED;
                        rightRotate(w);
                        w = x->parent->right;
                    }
                    w->color          = x->parent->color;      // Case 4
                    x->parent->color  = BLACK;
                    w->right->color   = BLACK;
                    leftRotate(x->parent);
                    x = root_;
                }
            } else {                                            // mirror
                Node* w = x->parent->left;
                if (w->color == RED) {
                    w->color         = BLACK;
                    x->parent->color = RED;
                    rightRotate(x->parent);
                    w = x->parent->left;
                }
                if (w->right->color == BLACK && w->left->color == BLACK) {
                    w->color = RED;
                    x = x->parent;
                } else {
                    if (w->left->color == BLACK) {
                        w->right->color = BLACK;
                        w->color        = RED;
                        leftRotate(w);
                        w = x->parent->left;
                    }
                    w->color         = x->parent->color;
                    x->parent->color = BLACK;
                    w->left->color   = BLACK;
                    rightRotate(x->parent);
                    x = root_;
                }
            }
        }
        x->color = BLACK;
    }

    // ── Traversal helpers ────────────────────────────────────────────────────

    void inorderHelper(Node* x) const {
        if (x == nil_) return;
        inorderHelper(x->left);
        std::cout << x->key << " ";
        inorderHelper(x->right);
    }

    void preorderHelper(Node* x) const {
        if (x == nil_) return;
        std::cout << x->key << " ";
        preorderHelper(x->left);
        preorderHelper(x->right);
    }

    void postorderHelper(Node* x) const {
        if (x == nil_) return;
        postorderHelper(x->left);
        postorderHelper(x->right);
        std::cout << x->key << " ";
    }

    // ── Visual print helper ──────────────────────────────────────────────────

    void printHelper(Node* node, const std::string& indent, bool last) const {
        if (node == nil_) return;
        std::cout << indent;
        std::cout << (last ? "└── " : "├── ");
        std::string colorTag = (node->color == RED) ? "\033[1;31mR\033[0m" : "\033[1;30mB\033[0m";
        std::cout << node->key << "(" << colorTag << ")\n";
        std::string childIndent = indent + (last ? "    " : "│   ");
        bool hasRight = (node->right != nil_);
        bool hasLeft  = (node->left  != nil_);
        if (hasLeft || hasRight) {
            if (hasRight) printHelper(node->right, childIndent, !hasLeft);
            if (hasLeft)  printHelper(node->left,  childIndent, true);
        }
    }

    // ── Validation helper ────────────────────────────────────────────────────

    bool validateHelper(Node* node, int& blackHeight) const {
        if (node == nil_) {
            blackHeight = 1;   // nil counts as 1 black node
            return true;
        }

        // No two consecutive red nodes
        if (node->color == RED) {
            if (node->left->color  == RED) return false;
            if (node->right->color == RED) return false;
        }

        // BST property
        if (node->left  != nil_ && node->left->key  >= node->key) return false;
        if (node->right != nil_ && node->right->key <= node->key) return false;

        int leftBH = 0, rightBH = 0;
        if (!validateHelper(node->left,  leftBH))  return false;
        if (!validateHelper(node->right, rightBH)) return false;

        // Black-height must be equal on both sides
        if (leftBH != rightBH) return false;

        blackHeight = leftBH + (node->color == BLACK ? 1 : 0);
        return true;
    }

    // ── Destructor helper ────────────────────────────────────────────────────

    void destroyTree(Node* node) {
        if (node == nil_) return;
        destroyTree(node->left);
        destroyTree(node->right);
        delete node;
    }
};