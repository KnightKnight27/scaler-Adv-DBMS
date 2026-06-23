#include "red-black-tree.h"

#include <utility>

RedBlackTree::RedBlackTree() : root_(nullptr), nil_(new Node(0, BLACK)) {
    nil_->left = nil_;
    nil_->right = nil_;
    nil_->parent = nil_;
    root_ = nil_;
}

RedBlackTree::~RedBlackTree() {
    destroy(root_);
    delete nil_;
}

void RedBlackTree::destroy(Node *node) {
    if (node == nil_ || node == nullptr) return;
    destroy(node->left);
    destroy(node->right);
    delete node;
}

void RedBlackTree::leftRotate(Node *x) {
    Node *y = x->right;
    x->right = y->left;
    if (y->left != nil_) {
        y->left->parent = x;
    }
    y->parent = x->parent;
    if (x->parent == nil_) {
        root_ = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
}

void RedBlackTree::rightRotate(Node *y) {
    Node *x = y->left;
    y->left = x->right;
    if (x->right != nil_) {
        x->right->parent = y;
    }
    x->parent = y->parent;
    if (y->parent == nil_) {
        root_ = x;
    } else if (y == y->parent->right) {
        y->parent->right = x;
    } else {
        y->parent->left = x;
    }
    x->right = y;
    y->parent = x;
}

void RedBlackTree::insert(int key) {
    Node *node = new Node(key, RED);
    node->left = nil_;
    node->right = nil_;

    Node *parent = nil_;
    Node *current = root_;

    while (current != nil_) {
        parent = current;
        if (node->key < current->key) {
            current = current->left;
        } else {
            current = current->right;
        }
    }

    node->parent = parent;
    if (parent == nil_) {
        root_ = node;
    } else if (node->key < parent->key) {
        parent->left = node;
    } else {
        parent->right = node;
    }

    insertFixup(node);
}

void RedBlackTree::insertFixup(Node *node) {
    while (node->parent->color == RED) {
        if (node->parent == node->parent->parent->left) {
            Node *uncle = node->parent->parent->right;
            if (uncle->color == RED) {
                node->parent->color = BLACK;
                uncle->color = BLACK;
                node->parent->parent->color = RED;
                node = node->parent->parent;
            } else {
                if (node == node->parent->right) {
                    node = node->parent;
                    leftRotate(node);
                }
                node->parent->color = BLACK;
                node->parent->parent->color = RED;
                rightRotate(node->parent->parent);
            }
        } else {
            Node *uncle = node->parent->parent->left;
            if (uncle->color == RED) {
                node->parent->color = BLACK;
                uncle->color = BLACK;
                node->parent->parent->color = RED;
                node = node->parent->parent;
            } else {
                if (node == node->parent->left) {
                    node = node->parent;
                    rightRotate(node);
                }
                node->parent->color = BLACK;
                node->parent->parent->color = RED;
                leftRotate(node->parent->parent);
            }
        }
    }
    root_->color = BLACK;
    root_->parent = nil_;
}

RedBlackTree::Node *RedBlackTree::minimum(Node *node) const {
    while (node->left != nil_) node = node->left;
    return node;
}

void RedBlackTree::transplant(Node *u, Node *v) {
    if (u->parent == nil_) {
        root_ = v;
    } else if (u == u->parent->left) {
        u->parent->left = v;
    } else {
        u->parent->right = v;
    }
    v->parent = u->parent;
}

RedBlackTree::Node *RedBlackTree::search(Node *node, int key) const {
    while (node != nil_ && node->key != key) {
        if (key < node->key) node = node->left;
        else node = node->right;
    }
    return node;
}

void RedBlackTree::remove(int key) {
    Node *z = search(root_, key);
    if (z == nil_) return;

    Node *y = z;
    Node *x = nullptr;
    bool y_original_black = (y->color == BLACK);

    if (z->left == nil_) {
        x = z->right;
        transplant(z, z->right);
    } else if (z->right == nil_) {
        x = z->left;
        transplant(z, z->left);
    } else {
        y = minimum(z->right);
        y_original_black = (y->color == BLACK);
        x = y->right;
        if (y->parent == z) {
            x->parent = y;
        } else {
            transplant(y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        transplant(z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }

    delete z;
    if (y_original_black) {
        deleteFixup(x);
    }
}

void RedBlackTree::deleteFixup(Node *x) {
    while (x != root_ && x->color == BLACK) {
        if (x == x->parent->left) {
            Node *w = x->parent->right;
            if (w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                leftRotate(x->parent);
                w = x->parent->right;
            }
            if (w->left->color == BLACK && w->right->color == BLACK) {
                w->color = RED;
                x = x->parent;
            } else {
                if (w->right->color == BLACK) {
                    w->left->color = BLACK;
                    w->color = RED;
                    rightRotate(w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->right->color = BLACK;
                leftRotate(x->parent);
                x = root_;
            }
        } else {
            Node *w = x->parent->left;
            if (w->color == RED) {
                w->color = BLACK;
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
                    w->color = RED;
                    leftRotate(w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->left->color = BLACK;
                rightRotate(x->parent);
                x = root_;
            }
        }
    }
    x->color = BLACK;
}

bool RedBlackTree::contains(int key) const {
    return search(root_, key) != nil_;
}

void RedBlackTree::printInOrder(std::ostream &out) const {
    printInOrder(root_, out);
    out << '\n';
}

void RedBlackTree::printInOrder(Node *node, std::ostream &out) const {
    if (node == nil_) return;
    printInOrder(node->left, out);
    out << node->key << '(' << (node->color == RED ? 'R' : 'B') << ") ";
    printInOrder(node->right, out);
}
