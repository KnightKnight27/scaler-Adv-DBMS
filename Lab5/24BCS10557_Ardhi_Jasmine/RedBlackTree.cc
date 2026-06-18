#include "RedBlackTree.h"

Node::Node(int value) {
    data = value;
    color = RED;
    left = right = parent = nullptr;
}

RedBlackTree::RedBlackTree() {
    root = nullptr;
}

void RedBlackTree::rotateLeft(Node* x) {
    Node* y = x->right;
    x->right = y->left;

    if (y->left != nullptr)
        y->left->parent = x;

    y->parent = x->parent;

    if (x->parent == nullptr)
        root = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;

    y->left = x;
    x->parent = y;
}

void RedBlackTree::rotateRight(Node* x) {
    Node* y = x->left;
    x->left = y->right;

    if (y->right != nullptr)
        y->right->parent = x;

    y->parent = x->parent;

    if (x->parent == nullptr)
        root = y;
    else if (x == x->parent->right)
        x->parent->right = y;
    else
        x->parent->left = y;

    y->right = x;
    x->parent = y;
}

void RedBlackTree::fixInsert(Node* node) {
    while (node != root && node->parent->color == RED) {
        Node* parent = node->parent;
        Node* grandparent = parent->parent;

        if (parent == grandparent->left) {
            Node* uncle = grandparent->right;

            if (uncle != nullptr && uncle->color == RED) {
                parent->color = BLACK;
                uncle->color = BLACK;
                grandparent->color = RED;
                node = grandparent;
            } else {
                if (node == parent->right) {
                    node = parent;
                    rotateLeft(node);
                }

                parent->color = BLACK;
                grandparent->color = RED;
                rotateRight(grandparent);
            }
        } else {
            Node* uncle = grandparent->left;

            if (uncle != nullptr && uncle->color == RED) {
                parent->color = BLACK;
                uncle->color = BLACK;
                grandparent->color = RED;
                node = grandparent;
            } else {
                if (node == parent->left) {
                    node = parent;
                    rotateRight(node);
                }

                parent->color = BLACK;
                grandparent->color = RED;
                rotateLeft(grandparent);
            }
        }
    }

    root->color = BLACK;
}

void RedBlackTree::insert(int value) {
    Node* newNode = new Node(value);
    Node* parent = nullptr;
    Node* current = root;

    while (current != nullptr) {
        parent = current;

        if (newNode->data < current->data)
            current = current->left;
        else
            current = current->right;
    }

    newNode->parent = parent;

    if (parent == nullptr)
        root = newNode;
    else if (newNode->data < parent->data)
        parent->left = newNode;
    else
        parent->right = newNode;

    fixInsert(newNode);
}

void RedBlackTree::inorderHelper(Node* node) {
    if (node == nullptr)
        return;

    inorderHelper(node->left);
    cout << node->data << "(" << (node->color == RED ? "R" : "B") << ") ";
    inorderHelper(node->right);
}

void RedBlackTree::preorderHelper(Node* node) {
    if (node == nullptr)
        return;

    cout << node->data << "(" << (node->color == RED ? "R" : "B") << ") ";
    preorderHelper(node->left);
    preorderHelper(node->right);
}

void RedBlackTree::inorder() {
    inorderHelper(root);
    cout << endl;
}

void RedBlackTree::preorder() {
    preorderHelper(root);
    cout << endl;
}