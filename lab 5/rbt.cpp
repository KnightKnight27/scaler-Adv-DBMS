#include "rbt.hpp"
#include <iostream>
#include <string>

using namespace std;

// ─── Constructor ──────────────────────────────────────────────────────────────

RedBlackTree::RedBlackTree() {
    // Sentinel NIL node: BLACK, no data
    NIL         = new Node(0);
    NIL->color  = BLACK;
    NIL->left   = nullptr;
    NIL->right  = nullptr;
    NIL->parent = nullptr;

    root = NIL;
}

// ─── Rotations ────────────────────────────────────────────────────────────────

void RedBlackTree::rotateLeft(Node* x) {
    Node* y   = x->right;
    x->right  = y->left;

    if (y->left != NIL)
        y->left->parent = x;

    y->parent = x->parent;

    if (x->parent == nullptr)
        root = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;

    y->left   = x;
    x->parent = y;
}

void RedBlackTree::rotateRight(Node* x) {
    Node* y  = x->left;
    x->left  = y->right;

    if (y->right != NIL)
        y->right->parent = x;

    y->parent = x->parent;

    if (x->parent == nullptr)
        root = y;
    else if (x == x->parent->right)
        x->parent->right = y;
    else
        x->parent->left = y;

    y->right  = x;
    x->parent = y;
}

// ─── Fix After Insert ─────────────────────────────────────────────────────────

void RedBlackTree::fixInsert(Node* z) {
    while (z->parent && z->parent->color == RED) {
        if (z->parent == z->parent->parent->left) {
            Node* y = z->parent->parent->right; // Uncle

            if (y->color == RED) {
                // Case 1: Uncle is RED — recolor
                z->parent->color          = BLACK;
                y->color                  = BLACK;
                z->parent->parent->color  = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    // Case 2: z is right child — rotate left
                    z = z->parent;
                    rotateLeft(z);
                }
                // Case 3: z is left child — rotate right
                z->parent->color         = BLACK;
                z->parent->parent->color = RED;
                rotateRight(z->parent->parent);
            }
        } else {
            // Mirror cases (parent is right child)
            Node* y = z->parent->parent->left; // Uncle

            if (y->color == RED) {
                // Case 1 (mirror)
                z->parent->color         = BLACK;
                y->color                 = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    // Case 2 (mirror)
                    z = z->parent;
                    rotateRight(z);
                }
                // Case 3 (mirror)
                z->parent->color         = BLACK;
                z->parent->parent->color = RED;
                rotateLeft(z->parent->parent);
            }
        }
    }
    root->color = BLACK; // Root is always BLACK
}

// ─── Insert ───────────────────────────────────────────────────────────────────

void RedBlackTree::insert(int value) {
    Node* z   = new Node(value);
    z->left   = NIL;
    z->right  = NIL;
    z->parent = nullptr;

    Node* y = nullptr;
    Node* x = root;

    // BST insert
    while (x != NIL) {
        y = x;
        if (z->data < x->data)
            x = x->left;
        else
            x = x->right;
    }

    z->parent = y;

    if (y == nullptr)
        root = z;
    else if (z->data < y->data)
        y->left = z;
    else
        y->right = z;

    // Fix Red-Black properties
    fixInsert(z);

    cout << "Inserted " << value
         << " into Red-Black Tree  [color: "
         << (z->color == RED ? "RED" : "BLACK") << "]\n";
}

// ─── Inorder Traversal ────────────────────────────────────────────────────────

void RedBlackTree::inorderHelper(Node* node) {
    if (node == NIL) return;
    inorderHelper(node->left);
    cout << node->data
         << "(" << (node->color == RED ? "R" : "B") << ") ";
    inorderHelper(node->right);
}

void RedBlackTree::inorder() {
    cout << "RBT Inorder Traversal: ";
    inorderHelper(root);
    cout << endl;
}

// ─── Visual Tree Print ────────────────────────────────────────────────────────

void RedBlackTree::printHelper(Node* node, string indent, bool last) {
    if (node != NIL) {
        cout << indent;
        if (last) {
            cout << "R----";
            indent += "     ";
        } else {
            cout << "L----";
            indent += "|    ";
        }
        string color = (node->color == RED) ? "RED" : "BLACK";
        cout << node->data << "(" << color << ")\n";
        printHelper(node->left,  indent, false);
        printHelper(node->right, indent, true);
    }
}

void RedBlackTree::printTree() {
    cout << "\n=== Red-Black Tree Structure ===\n";
    if (root != NIL)
        printHelper(root, "", true);
    cout << "================================\n";
}

// ─── Search ───────────────────────────────────────────────────────────────────

Node* RedBlackTree::search(int value) {
    Node* current = root;
    while (current != NIL) {
        if (value == current->data) return current;
        current = (value < current->data) ? current->left : current->right;
    }
    return NIL;
}