#include "RedBlackTree.h"

// LEFT ROTATION
void RedBlackTree::rotateLeft(Node*& root, Node*& pt) {
    Node* pt_right = pt->right;

    pt->right = pt_right->left;
    if (pt->right != nullptr)
        pt->right->parent = pt;

    pt_right->parent = pt->parent;

    if (pt->parent == nullptr)
        root = pt_right;
    else if (pt == pt->parent->left)
        pt->parent->left = pt_right;
    else
        pt->parent->right = pt_right;

    pt_right->left = pt;
    pt->parent = pt_right;
}

// RIGHT ROTATION
void RedBlackTree::rotateRight(Node*& root, Node*& pt) {
    Node* pt_left = pt->left;

    pt->left = pt_left->right;
    if (pt->left != nullptr)
        pt->left->parent = pt;

    pt_left->parent = pt->parent;

    if (pt->parent == nullptr)
        root = pt_left;
    else if (pt == pt->parent->left)
        pt->parent->left = pt_left;
    else
        pt->parent->right = pt_left;

    pt_left->right = pt;
    pt->parent = pt_left;
}

// FIX VIOLATIONS AFTER INSERT
void RedBlackTree::fixInsert(Node*& root, Node*& pt) {
    Node *parent_pt = nullptr;
    Node *grand_parent_pt = nullptr;

    while ((pt != root) && (pt->color != BLACK) &&
           (pt->parent->color == RED)) {

        parent_pt = pt->parent;
        grand_parent_pt = pt->parent->parent;

        // Parent is left child
        if (parent_pt == grand_parent_pt->left) {

            Node* uncle_pt = grand_parent_pt->right;

            // Case 1: Uncle is RED
            if (uncle_pt != nullptr && uncle_pt->color == RED) {
                grand_parent_pt->color = RED;
                parent_pt->color = BLACK;
                uncle_pt->color = BLACK;
                pt = grand_parent_pt;
            }
            else {
                // Case 2: pt is right child
                if (pt == parent_pt->right) {
                    rotateLeft(root, parent_pt);
                    pt = parent_pt;
                    parent_pt = pt->parent;
                }

                // Case 3
                rotateRight(root, grand_parent_pt);
                swap(parent_pt->color, grand_parent_pt->color);
                pt = parent_pt;
            }
        }
        else {
            Node* uncle_pt = grand_parent_pt->left;

            if (uncle_pt != nullptr && uncle_pt->color == RED) {
                grand_parent_pt->color = RED;
                parent_pt->color = BLACK;
                uncle_pt->color = BLACK;
                pt = grand_parent_pt;
            }
            else {
                if (pt == parent_pt->left) {
                    rotateRight(root, parent_pt);
                    pt = parent_pt;
                    parent_pt = pt->parent;
                }

                rotateLeft(root, grand_parent_pt);
                swap(parent_pt->color, grand_parent_pt->color);
                pt = parent_pt;
            }
        }
    }

    root->color = BLACK;
}

// INSERT FUNCTION
void RedBlackTree::insert(int data) {
    Node* pt = new Node(data);

    Node* y = nullptr;
    Node* x = root;

    while (x != nullptr) {
        y = x;
        if (pt->data < x->data)
            x = x->left;
        else
            x = x->right;
    }

    pt->parent = y;

    if (y == nullptr)
        root = pt;
    else if (pt->data < y->data)
        y->left = pt;
    else
        y->right = pt;

    fixInsert(root, pt);
}

// INORDER TRAVERSAL
void RedBlackTree::inorderHelper(Node* root) {
    if (root == nullptr)
        return;

    inorderHelper(root->left);
    cout << root->data << " ";
    inorderHelper(root->right);
}

void RedBlackTree::inorder() {
    inorderHelper(root);
}