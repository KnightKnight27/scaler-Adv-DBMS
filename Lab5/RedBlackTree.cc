// red black tree impl, based on clrs
// lol this took me forever to debug
#include "RedBlackTree.h"
#include <string>

RedBlackTree::RedBlackTree() {
    NIL = new Node(0);
    NIL->color = black;
    NIL->left = NIL->right = NIL->parent = nullptr;
    root = NIL;
}

RedBlackTree::~RedBlackTree() {
    deleteTree(root);
    delete NIL;
}

void RedBlackTree::deleteTree(Node *node)
{
    if (node == nullptr) return;  // redundant but safe
    if (node == NIL || node == nullptr) return;
    deleteTree(node->left);
    this->deleteTree(node->right);
    delete node;
}

void RedBlackTree::rotateLeft(Node *x)
{
    Node *y = x->right;
    x->right = y->left;
    if (y->left != NIL) y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == nullptr) root = y;
    else if (x == x->parent->left) x->parent->left  = y;
    else                           x->parent->right = y;
    y->left   = x;
    x->parent = y;
}

void RedBlackTree::rotateRight(Node *x)
{
    Node *y = x->left;
    x->left = y->right;
    if (y->right != NIL) y->right->parent = x;
    y->parent = x->parent;
    if (x->parent == nullptr) root = y;
    else if (x == x->parent->right) x->parent->right = y;
    else                            x->parent->left  = y;
    y->right = x;
    x->parent = y;
}

void RedBlackTree::insert(int val)
{
    Node *node = new Node(val);
    node->left = node->right = NIL;

    Node *parent = nullptr;
    Node *trav = root;

    // traversing down the treee
    while (trav != NIL) {
        parent = trav;
        if (val < trav->val) trav = trav->left;
        else                 trav = trav->right;
    }

    node->parent = parent;
    if (parent == nullptr) root = node;
    else if (val < parent->val) parent->left  = node;
    else                        parent->right = node;

    // root is always black
    if (node->parent == nullptr) { node->color = black; return; }
    // TODO: might leak memory on early return but idk
    if (node->parent->parent == nullptr) return;

    fixInsert(node);
}

void RedBlackTree::fixInsert(Node *node)
{
    // std::cout << "fixing " << node->val << "\n";
    while (node->parent && node->parent->color == red) {
        if (node->parent == node->parent->parent->left) {
            Node *u = node->parent->parent->right;  // uncle
            if (u->color == red) {
                // case 1: uncle is red (i think)
                node->parent->color = black;
                u->color = black;
                node->parent->parent->color = red;
                node = node->parent->parent;
            } else {
                if (node == node->parent->right) {
                    // case 2 fix
                    node = node->parent;
                    rotateLeft(node);
                }
                // case 3
                node->parent->color = black;
                node->parent->parent->color = red;
                rotateRight(node->parent->parent);
            }
        } else {
            Node *uncle = node->parent->parent->left;
            if (uncle->color == red) {
                node->parent->color = black;
                uncle->color = black;
                node->parent->parent->color = red;
                node = node->parent->parent;
            } else {
                if (node == node->parent->left) {
                    node = node->parent;
                    rotateRight(node);
                }
                node->parent->color = black;
                node->parent->parent->color = red;
                rotateLeft(node->parent->parent);
            }
        }
        if (node == root) break;
    }
    root->color = black;
}

bool RedBlackTree::find(int val)
{
    Node *trav = root;
    while (trav != NIL) {
        if (val == trav->val) {
            return true;
        }
        if (val < trav->val) {
            trav = trav->left;
        } else {
            trav = trav->right;
        }
    }
    return false;
}

void RedBlackTree::transplant(Node *u, Node *v) {
    if (u->parent == nullptr) root = v;
    else if (u == u->parent->left) u->parent->left = v;
    else u->parent->right = v;
    v->parent = u->parent;
}

RedBlackTree::Node *RedBlackTree::minimum(Node *node) {
    while (node->left != NIL) node = node->left;
    return node;
}

void RedBlackTree::remove(int val) {
    Node *z = root;
    while (z != NIL) {
        if (val == z->val) break;
        z = (val < z->val) ? z->left : z->right;
    }
    if (z == NIL) return;

    Node *y = z;
    Node *x;
    Color yOrigColor = y->color;

    if (z->left == NIL) {
        x = z->right;
        transplant(z, z->right);
    } else if (z->right == NIL) {
        x = z->left;
        transplant(z, z->left);
    } else {
        y = minimum(z->right);
        yOrigColor = y->color;
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
    if (yOrigColor == black) fixDelete(x);
}

void RedBlackTree::fixDelete(Node *x) {
    while (x != root && x->color == black) {
        if (x == x->parent->left) {
            Node *w = x->parent->right;
            if (w->color == red) {
                w->color = black;
                x->parent->color = red;
                rotateLeft(x->parent);
                w = x->parent->right;
            }
            if (w->left->color == black && w->right->color == black) {
                w->color = red;
                x = x->parent;
            } else {
                if (w->right->color == black) {
                    w->left->color = black;
                    w->color = red;
                    rotateRight(w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;
                x->parent->color = black;
                w->right->color = black;
                rotateLeft(x->parent);
                x = root;
            }
        } else {
            Node *w = x->parent->left;
            if (w->color == red) {
                w->color = black;
                x->parent->color = red;
                rotateRight(x->parent);
                w = x->parent->left;
            }
            if (w->right->color == black && w->left->color == black) {
                w->color = red;
                x = x->parent;
            } else {
                if (w->left->color == black) {
                    w->right->color = black;
                    w->color = red;
                    rotateLeft(w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = black;
                w->left->color = black;
                rotateRight(x->parent);
                x = root;
            }
        }
    }
    x->color = black;
}

void RedBlackTree::printHelper(Node *node, std::string indent, bool last) {
    if (node != NIL) {
        std::cout << indent;
        if (last) { std::cout << "R----"; indent += "     "; }
        else       { std::cout << "L----"; indent += "|    "; }
        std::string color = (node->color == red) ? "RED" : "BLACK";
        std::cout << node->val << " (" << color << ")\n";
        printHelper(node->left,  indent, false);
        printHelper(node->right, indent, true);
    }
}

void RedBlackTree::print() {
    if (root != NIL) printHelper(root, "", true);
}
