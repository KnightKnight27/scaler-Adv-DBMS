#include "rbt.hpp"

#include <iostream>

RBNode::RBNode(int value)
    : key(value), color(Color::Red), left(nullptr), right(nullptr), parent(nullptr) {}

RedBlackTree::RedBlackTree() {
    nil = new RBNode(0);
    nil->color = Color::Black;
    nil->left = nil;
    nil->right = nil;
    nil->parent = nullptr;
    root = nil;
}

RedBlackTree::~RedBlackTree() {
    destroy(root);
    delete nil;
}

void RedBlackTree::destroy(RBNode* node) {
    if (node == nil) {
        return;
    }
    destroy(node->left);
    destroy(node->right);
    delete node;
}

void RedBlackTree::rotateLeft(RBNode* node) {
    RBNode* pivot = node->right;
    node->right = pivot->left;

    if (pivot->left != nil) {
        pivot->left->parent = node;
    }

    pivot->parent = node->parent;
    if (node->parent == nullptr) {
        root = pivot;
    } else if (node == node->parent->left) {
        node->parent->left = pivot;
    } else {
        node->parent->right = pivot;
    }

    pivot->left = node;
    node->parent = pivot;
}

void RedBlackTree::rotateRight(RBNode* node) {
    RBNode* pivot = node->left;
    node->left = pivot->right;

    if (pivot->right != nil) {
        pivot->right->parent = node;
    }

    pivot->parent = node->parent;
    if (node->parent == nullptr) {
        root = pivot;
    } else if (node == node->parent->right) {
        node->parent->right = pivot;
    } else {
        node->parent->left = pivot;
    }

    pivot->right = node;
    node->parent = pivot;
}

void RedBlackTree::repairAfterInsert(RBNode* node) {
    while (node->parent != nullptr && node->parent->color == Color::Red) {
        RBNode* grandparent = node->parent->parent;

        if (node->parent == grandparent->left) {
            RBNode* uncle = grandparent->right;
            if (uncle->color == Color::Red) {
                node->parent->color = Color::Black;
                uncle->color = Color::Black;
                grandparent->color = Color::Red;
                node = grandparent;
            } else {
                if (node == node->parent->right) {
                    node = node->parent;
                    rotateLeft(node);
                }
                node->parent->color = Color::Black;
                grandparent->color = Color::Red;
                rotateRight(grandparent);
            }
        } else {
            RBNode* uncle = grandparent->left;
            if (uncle->color == Color::Red) {
                node->parent->color = Color::Black;
                uncle->color = Color::Black;
                grandparent->color = Color::Red;
                node = grandparent;
            } else {
                if (node == node->parent->left) {
                    node = node->parent;
                    rotateRight(node);
                }
                node->parent->color = Color::Black;
                grandparent->color = Color::Red;
                rotateLeft(grandparent);
            }
        }
    }

    root->color = Color::Black;
}

void RedBlackTree::insert(int key) {
    RBNode* node = new RBNode(key);
    node->left = nil;
    node->right = nil;

    RBNode* parent = nullptr;
    RBNode* current = root;

    while (current != nil) {
        parent = current;
        current = key < current->key ? current->left : current->right;
    }

    node->parent = parent;
    if (parent == nullptr) {
        root = node;
    } else if (key < parent->key) {
        parent->left = node;
    } else {
        parent->right = node;
    }

    repairAfterInsert(node);
}

const RBNode* RedBlackTree::search(int key) const {
    RBNode* current = root;
    while (current != nil) {
        if (key == current->key) {
            return current;
        }
        current = key < current->key ? current->left : current->right;
    }

    return nullptr;
}

void RedBlackTree::inorderFrom(const RBNode* node) const {
    if (node == nil) {
        return;
    }

    inorderFrom(node->left);
    std::cout << node->key << '(' << (node->color == Color::Red ? 'R' : 'B') << ") ";
    inorderFrom(node->right);
}

void RedBlackTree::inorder() const {
    std::cout << "Red-Black Tree inorder: ";
    inorderFrom(root);
    std::cout << '\n';
}

void RedBlackTree::printFrom(const RBNode* node,
                             const std::string& prefix,
                             bool isLast) const {
    if (node == nil) {
        return;
    }

    std::cout << prefix << (isLast ? "R-- " : "L-- ") << node->key
              << (node->color == Color::Red ? "(RED)" : "(BLACK)") << '\n';

    std::string nextPrefix = prefix + (isLast ? "    " : "|   ");
    printFrom(node->left, nextPrefix, false);
    printFrom(node->right, nextPrefix, true);
}

void RedBlackTree::printStructure() const {
    std::cout << "\nRed-Black Tree structure\n";
    if (root == nil) {
        std::cout << "empty\n";
        return;
    }
    printFrom(root, "", true);
}