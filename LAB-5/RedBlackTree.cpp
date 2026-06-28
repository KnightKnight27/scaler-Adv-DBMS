// Lab 5 - Red-Black Tree (implementation)
// Bhavya Jain (23BCS10088) <Bhavya.23bcs10088@sst.scaler.com>

#include "RedBlackTree.h"

#include <iostream>
#include <queue>
#include <string>

RedBlackTree::RedBlackTree() : root_(nullptr), nil_(nullptr), size_(0) {
    nil_         = new Node{0, BLACK, nullptr, nullptr, nullptr};
    nil_->left   = nil_;
    nil_->right  = nil_;
    nil_->parent = nil_;
    root_        = nil_;
}

RedBlackTree::~RedBlackTree() {
    destroy(root_);
    delete nil_;
}

void RedBlackTree::destroy(Node* n) {
    if (n == nil_ || n == nullptr) return;
    destroy(n->left);
    destroy(n->right);
    delete n;
}

RedBlackTree::Node* RedBlackTree::newNode(int key, Color col, Node* parent) {
    return new Node{key, col, nil_, nil_, parent};
}

bool RedBlackTree::find(int key) const {
    return findNode(key) != nil_;
}

RedBlackTree::Node* RedBlackTree::findNode(int key) const {
    Node* cur = root_;
    while (cur != nil_) {
        if (key == cur->key) return cur;
        cur = (key < cur->key) ? cur->left : cur->right;
    }
    return nil_;
}

void RedBlackTree::leftRotate(Node* x) {
    Node* y  = x->right;
    x->right = y->left;
    if (y->left != nil_) y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == nil_)            root_                = y;
    else if (x == x->parent->left)    x->parent->left      = y;
    else                              x->parent->right     = y;
    y->left   = x;
    x->parent = y;
}

void RedBlackTree::rightRotate(Node* x) {
    Node* y  = x->left;
    x->left  = y->right;
    if (y->right != nil_) y->right->parent = x;
    y->parent = x->parent;
    if (x->parent == nil_)            root_                = y;
    else if (x == x->parent->right)   x->parent->right     = y;
    else                              x->parent->left      = y;
    y->right  = x;
    x->parent = y;
}

void RedBlackTree::insert(int key) {
    Node* y = nil_;
    Node* x = root_;
    while (x != nil_) {
        y = x;
        x = (key < x->key) ? x->left : x->right;
    }

    Node* z = newNode(key, RED, y);
    if (y == nil_)            root_      = z;
    else if (key < y->key)    y->left    = z;
    else                      y->right   = z;

    ++size_;
    insertFixup(z);
}

void RedBlackTree::insertFixup(Node* z) {
    while (z->parent->col == RED) {
        Node* gp = z->parent->parent;
        if (z->parent == gp->left) {
            Node* uncle = gp->right;
            if (uncle->col == RED) {            // Case 1: recolor and recurse on grandparent
                z->parent->col = BLACK;
                uncle->col     = BLACK;
                gp->col        = RED;
                z              = gp;
            } else {
                if (z == z->parent->right) {     // Case 2: rotate left to reduce to case 3
                    z = z->parent;
                    leftRotate(z);
                }
                z->parent->col = BLACK;          // Case 3
                gp->col        = RED;
                rightRotate(gp);
            }
        } else {                                 // Mirror of the above
            Node* uncle = gp->left;
            if (uncle->col == RED) {
                z->parent->col = BLACK;
                uncle->col     = BLACK;
                gp->col        = RED;
                z              = gp;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rightRotate(z);
                }
                z->parent->col = BLACK;
                gp->col        = RED;
                leftRotate(gp);
            }
        }
    }
    root_->col = BLACK;
}

void RedBlackTree::transplant(Node* u, Node* v) {
    if (u->parent == nil_)            root_              = v;
    else if (u == u->parent->left)    u->parent->left    = v;
    else                              u->parent->right   = v;
    v->parent = u->parent;
}

RedBlackTree::Node* RedBlackTree::minimum(Node* x) const {
    while (x->left != nil_) x = x->left;
    return x;
}

bool RedBlackTree::erase(int key) {
    Node* z = findNode(key);
    if (z == nil_) return false;

    Node* y          = z;
    Color yOrigColor = y->col;
    Node* x;

    if (z->left == nil_) {
        x = z->right;
        transplant(z, z->right);
    } else if (z->right == nil_) {
        x = z->left;
        transplant(z, z->left);
    } else {
        y          = minimum(z->right);
        yOrigColor = y->col;
        x          = y->right;
        if (y->parent == z) {
            x->parent = y;                       // keeps sentinel parent link correct
        } else {
            transplant(y, y->right);
            y->right         = z->right;
            y->right->parent = y;
        }
        transplant(z, y);
        y->left         = z->left;
        y->left->parent = y;
        y->col          = z->col;
    }

    delete z;
    --size_;

    if (yOrigColor == BLACK) eraseFixup(x);
    nil_->parent = nil_;                         // scrub any leak from the fixup
    return true;
}

void RedBlackTree::eraseFixup(Node* x) {
    while (x != root_ && x->col == BLACK) {
        if (x == x->parent->left) {
            Node* w = x->parent->right;
            if (w->col == RED) {                 // Case 1
                w->col         = BLACK;
                x->parent->col = RED;
                leftRotate(x->parent);
                w = x->parent->right;
            }
            if (w->left->col == BLACK && w->right->col == BLACK) {  // Case 2
                w->col = RED;
                x      = x->parent;
            } else {
                if (w->right->col == BLACK) {    // Case 3
                    w->left->col = BLACK;
                    w->col       = RED;
                    rightRotate(w);
                    w = x->parent->right;
                }
                w->col         = x->parent->col; // Case 4
                x->parent->col = BLACK;
                w->right->col  = BLACK;
                leftRotate(x->parent);
                x = root_;
            }
        } else {                                 // Mirror of the above
            Node* w = x->parent->left;
            if (w->col == RED) {
                w->col         = BLACK;
                x->parent->col = RED;
                rightRotate(x->parent);
                w = x->parent->left;
            }
            if (w->right->col == BLACK && w->left->col == BLACK) {
                w->col = RED;
                x      = x->parent;
            } else {
                if (w->left->col == BLACK) {
                    w->right->col = BLACK;
                    w->col        = RED;
                    leftRotate(w);
                    w = x->parent->left;
                }
                w->col         = x->parent->col;
                x->parent->col = BLACK;
                w->left->col   = BLACK;
                rightRotate(x->parent);
                x = root_;
            }
        }
    }
    x->col = BLACK;
}

void RedBlackTree::print() const {
    if (root_ == nil_) { std::cout << "[]\n"; return; }

    std::vector<std::string> out;
    std::queue<Node*> q;
    q.push(root_);
    while (!q.empty()) {
        Node* n = q.front();
        q.pop();
        if (n == nil_) {
            out.emplace_back("null");
        } else {
            out.emplace_back(std::to_string(n->key));
            q.push(n->left);
            q.push(n->right);
        }
    }
    while (!out.empty() && out.back() == "null") out.pop_back();

    std::cout << "[";
    for (size_t i = 0; i < out.size(); ++i) {
        std::cout << out[i];
        if (i + 1 < out.size()) std::cout << ", ";
    }
    std::cout << "]\n";
}

void RedBlackTree::inorderInto(Node* n, std::vector<int>& out) const {
    if (n == nil_) return;
    inorderInto(n->left, out);
    out.push_back(n->key);
    inorderInto(n->right, out);
}

std::vector<int> RedBlackTree::inorder() const {
    std::vector<int> out;
    out.reserve(size_);
    inorderInto(root_, out);
    return out;
}

int RedBlackTree::blackHeight(Node* n, bool& ok) const {
    if (n == nil_) return 1;
    if (n->col == RED && (n->left->col == RED || n->right->col == RED)) ok = false;
    int lh = blackHeight(n->left, ok);
    int rh = blackHeight(n->right, ok);
    if (lh != rh) ok = false;
    return lh + (n->col == BLACK ? 1 : 0);
}

bool RedBlackTree::validate() const {
    if (root_->col != BLACK) return false;
    bool ok = true;
    blackHeight(root_, ok);
    return ok;
}
