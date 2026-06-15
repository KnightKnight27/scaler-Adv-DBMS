// Lab 5 - Red-Black Tree (implementation)
//
// Reference: Cormen, Leiserson, Rivest, Stein - "Introduction to
// Algorithms" 3e, ch. 13. Insertion and deletion both use a single
// shared NIL sentinel (`nil_`) so we can read `x->parent`, `x->left`,
// `x->color` without nullptr guards.

#include "RedBlackTree.h"

#include <cassert>
#include <iostream>
#include <queue>

RedBlackTree::RedBlackTree() {
    nil_ = new Node{0, Color::BLACK, nullptr, nullptr, nullptr};
    root_ = nil_;
}

RedBlackTree::~RedBlackTree() {
    destroy(root_);
    delete nil_;
}

void RedBlackTree::insert(int key) {
    Node* z = new Node{key, Color::RED, nil_, nil_, nil_};

    Node* y = nil_;
    Node* x = root_;
    while (x != nil_) {
        y = x;
        x = (z->key < x->key) ? x->left : x->right;
    }
    z->parent = y;
    if (y == nil_)            root_ = z;
    else if (z->key < y->key) y->left  = z;
    else                      y->right = z;

    insertFixup(z);
}

bool RedBlackTree::find(int key) const {
    return findNode(key) != nil_;
}

bool RedBlackTree::remove(int key) {
    Node* z = findNode(key);
    if (z == nil_) return false;

    Node* y = z;
    Color yOriginalColor = y->color;
    Node* x;

    if (z->left == nil_) {
        x = z->right;
        transplant(z, z->right);
    } else if (z->right == nil_) {
        x = z->left;
        transplant(z, z->left);
    } else {
        y = minimum(z->right);
        yOriginalColor = y->color;
        x = y->right;
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

    if (yOriginalColor == Color::BLACK) deleteFixup(x);
    return true;
}

void RedBlackTree::print() const {
    if (root_ == nil_) {
        std::cout << "(empty tree)\n";
        return;
    }

    std::queue<std::pair<Node*, int>> q;
    q.push({root_, 0});
    int currentDepth = -1;

    while (!q.empty()) {
        auto [node, depth] = q.front();
        q.pop();

        if (depth != currentDepth) {
            if (currentDepth != -1) std::cout << '\n';
            std::cout << "L" << depth << ":";
            currentDepth = depth;
        }

        const char* colorTag = (node->color == Color::RED) ? "R" : "B";
        std::cout << "  " << node->key << "(" << colorTag << ")";

        if (node->left  != nil_) q.push({node->left,  depth + 1});
        if (node->right != nil_) q.push({node->right, depth + 1});
    }
    std::cout << '\n';
}

int RedBlackTree::checkInvariants() const {
    if (root_ != nil_) {
        assert(root_->color == Color::BLACK && "RB invariant 2: root must be BLACK");
    }
    return blackHeightOf(root_);
}

// ---------------- private helpers ----------------

Node* RedBlackTree::findNode(int key) const {
    Node* x = root_;
    while (x != nil_ && x->key != key)
        x = (key < x->key) ? x->left : x->right;
    return x;
}

Node* RedBlackTree::minimum(Node* x) const {
    while (x->left != nil_) x = x->left;
    return x;
}

void RedBlackTree::leftRotate(Node* x) {
    Node* y = x->right;
    x->right = y->left;
    if (y->left != nil_) y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == nil_)         root_ = y;
    else if (x == x->parent->left) x->parent->left  = y;
    else                           x->parent->right = y;
    y->left   = x;
    x->parent = y;
}

void RedBlackTree::rightRotate(Node* x) {
    Node* y = x->left;
    x->left = y->right;
    if (y->right != nil_) y->right->parent = x;
    y->parent = x->parent;
    if (x->parent == nil_)          root_ = y;
    else if (x == x->parent->right) x->parent->right = y;
    else                            x->parent->left  = y;
    y->right  = x;
    x->parent = y;
}

void RedBlackTree::insertFixup(Node* z) {
    while (z->parent->color == Color::RED) {
        if (z->parent == z->parent->parent->left) {
            Node* y = z->parent->parent->right;
            if (y->color == Color::RED) {                     // case 1: red uncle
                z->parent->color         = Color::BLACK;
                y->color                 = Color::BLACK;
                z->parent->parent->color = Color::RED;
                z                        = z->parent->parent;
            } else {
                if (z == z->parent->right) {                  // case 2: zigzag
                    z = z->parent;
                    leftRotate(z);
                }
                z->parent->color         = Color::BLACK;      // case 3: straight
                z->parent->parent->color = Color::RED;
                rightRotate(z->parent->parent);
            }
        } else {
            Node* y = z->parent->parent->left;
            if (y->color == Color::RED) {
                z->parent->color         = Color::BLACK;
                y->color                 = Color::BLACK;
                z->parent->parent->color = Color::RED;
                z                        = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rightRotate(z);
                }
                z->parent->color         = Color::BLACK;
                z->parent->parent->color = Color::RED;
                leftRotate(z->parent->parent);
            }
        }
    }
    root_->color = Color::BLACK;
}

void RedBlackTree::transplant(Node* u, Node* v) {
    if (u->parent == nil_)         root_ = v;
    else if (u == u->parent->left) u->parent->left  = v;
    else                           u->parent->right = v;
    v->parent = u->parent;
}

void RedBlackTree::deleteFixup(Node* x) {
    while (x != root_ && x->color == Color::BLACK) {
        if (x == x->parent->left) {
            Node* w = x->parent->right;
            if (w->color == Color::RED) {                     // case 1
                w->color         = Color::BLACK;
                x->parent->color = Color::RED;
                leftRotate(x->parent);
                w = x->parent->right;
            }
            if (w->left->color  == Color::BLACK &&
                w->right->color == Color::BLACK) {            // case 2
                w->color = Color::RED;
                x        = x->parent;
            } else {
                if (w->right->color == Color::BLACK) {        // case 3
                    w->left->color = Color::BLACK;
                    w->color       = Color::RED;
                    rightRotate(w);
                    w = x->parent->right;
                }
                w->color         = x->parent->color;          // case 4
                x->parent->color = Color::BLACK;
                w->right->color  = Color::BLACK;
                leftRotate(x->parent);
                x = root_;
            }
        } else {
            Node* w = x->parent->left;
            if (w->color == Color::RED) {
                w->color         = Color::BLACK;
                x->parent->color = Color::RED;
                rightRotate(x->parent);
                w = x->parent->left;
            }
            if (w->right->color == Color::BLACK &&
                w->left->color  == Color::BLACK) {
                w->color = Color::RED;
                x        = x->parent;
            } else {
                if (w->left->color == Color::BLACK) {
                    w->right->color = Color::BLACK;
                    w->color        = Color::RED;
                    leftRotate(w);
                    w = x->parent->left;
                }
                w->color         = x->parent->color;
                x->parent->color = Color::BLACK;
                w->left->color   = Color::BLACK;
                rightRotate(x->parent);
                x = root_;
            }
        }
    }
    x->color = Color::BLACK;
}

void RedBlackTree::destroy(Node* x) {
    if (x == nil_) return;
    destroy(x->left);
    destroy(x->right);
    delete x;
}

int RedBlackTree::blackHeightOf(Node* x) const {
    if (x == nil_) return 1;
    int lh = blackHeightOf(x->left);
    int rh = blackHeightOf(x->right);
    assert(lh == rh && "RB invariant 5: unequal black-height");
    if (x->color == Color::RED) {
        assert(x->left->color  == Color::BLACK &&
               x->right->color == Color::BLACK &&
               "RB invariant 4: red node has red child");
    }
    return lh + (x->color == Color::BLACK ? 1 : 0);
}
