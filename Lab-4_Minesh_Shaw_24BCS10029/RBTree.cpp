#include "RBTree.hpp"

RBTree::RBTree() {
    TNULL = new Node;
    TNULL->color = BLACK;
    TNULL->left = nullptr;
    TNULL->right = nullptr;
    root = TNULL;
}

RBTree::~RBTree() {
    destroyTree(root);
    delete TNULL;
}

void RBTree::destroyTree(Node* node) {
    if (node != TNULL) {
        destroyTree(node->left);
        destroyTree(node->right);
        delete node;
    }
}

void RBTree::preOrderHelper(Node* node) const {
    if (node != TNULL) {
        std::cout << node->data << "(" << (node->color == RED ? "R" : "B") << ") ";
        preOrderHelper(node->left);
        preOrderHelper(node->right);
    }
}

Node* RBTree::searchTree(int key) {
    Node* node = root;
    while (node != TNULL && key != node->data) {
        if (key < node->data) node = node->left;
        else node = node->right;
    }
    return node;
}

Node* RBTree::minimum(Node* node) {
    while (node->left != TNULL) {
        node = node->left;
    }
    return node;
}

void RBTree::leftRotate(Node* x) {
    Node* y = x->right;
    x->right = y->left;
    if (y->left != TNULL) y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == nullptr) root = y;
    else if (x == x->parent->left) x->parent->left = y;
    else x->parent->right = y;
    y->left = x;
    x->parent = y;
}

void RBTree::rightRotate(Node* x) {
    Node* y = x->left;
    x->left = y->right;
    if (y->right != TNULL) y->right->parent = x;
    y->parent = x->parent;
    if (x->parent == nullptr) root = y;
    else if (x == x->parent->right) x->parent->right = y;
    else x->parent->left = y;
    y->right = x;
    x->parent = y;
}

void RBTree::insert(int key) {
    Node* node = new Node{key, RED, TNULL, TNULL, nullptr};
    Node* y = nullptr;
    Node* x = root;

    while (x != TNULL) {
        y = x;
        if (node->data < x->data) x = x->left;
        else x = x->right;
    }

    node->parent = y;
    if (y == nullptr) root = node;
    else if (node->data < y->data) y->left = node;
    else y->right = node;

    if (node->parent == nullptr) {
        node->color = BLACK;
        return;
    }
    if (node->parent->parent == nullptr) return;

    insertFixup(node);
}

void RBTree::insertFixup(Node* k) {
    Node* u;
    while (k->parent->color == RED) {
        if (k->parent == k->parent->parent->right) {
            u = k->parent->parent->left; // uncle
            if (u->color == RED) {
                u->color = BLACK;
                k->parent->color = BLACK;
                k->parent->parent->color = RED;
                k = k->parent->parent;
            } else {
                if (k == k->parent->left) {
                    k = k->parent;
                    rightRotate(k);
                }
                k->parent->color = BLACK;
                k->parent->parent->color = RED;
                leftRotate(k->parent->parent);
            }
        } else {
            u = k->parent->parent->right; // uncle
            if (u->color == RED) {
                u->color = BLACK;
                k->parent->color = BLACK;
                k->parent->parent->color = RED;
                k = k->parent->parent;
            } else {
                if (k == k->parent->right) {
                    k = k->parent;
                    leftRotate(k);
                }
                k->parent->color = BLACK;
                k->parent->parent->color = RED;
                rightRotate(k->parent->parent);
            }
        }
        if (k == root) break;
    }
    root->color = BLACK;
}

void RBTree::transplant(Node* u, Node* v) {
    if (u->parent == nullptr) root = v;
    else if (u == u->parent->left) u->parent->left = v;
    else u->parent->right = v;
    v->parent = u->parent;
}

void RBTree::deleteNode(int data) {
    Node* z = searchTree(data);
    if (z == TNULL) return;

    Node* y = z;
    Node* x;
    Color y_original_color = y->color;

    if (z->left == TNULL) {
        x = z->right;
        transplant(z, z->right);
    } else if (z->right == TNULL) {
        x = z->left;
        transplant(z, z->left);
    } else {
        y = minimum(z->right);
        y_original_color = y->color;
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

    if (y_original_color == BLACK) {
        deleteFixup(x);
    }
}

void RBTree::deleteFixup(Node* x) {
    Node* w;
    while (x != root && x->color == BLACK) {
        if (x == x->parent->left) {
            w = x->parent->right;
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
                x = root;
            }
        } else {
            w = x->parent->left;
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
                x = root;
            }
        }
    }
    x->color = BLACK;
}

void RBTree::printTree() const {
    if (root != TNULL) {
        preOrderHelper(root);
        std::cout << "\n";
    }
}