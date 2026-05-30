#include <iostream>
using namespace std;

enum Color {
    RED,
    BLACK
};

struct Node {
    int data;
    Color color;
    Node *left, *right, *parent;

    Node(int data) {
        this->data = data;
        left = right = parent = nullptr;
        color = RED;
    }
};

class RedBlackTree {
private:
    Node* root;

    void rotateLeft(Node*& pt) {
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

    void rotateRight(Node*& pt) {
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

    void fixViolation(Node*& pt) {
        Node* parent_pt = nullptr;
        Node* grand_parent_pt = nullptr;

        while ((pt != root) &&
               (pt->color != BLACK) &&
               (pt->parent->color == RED)) {

            parent_pt = pt->parent;
            grand_parent_pt = pt->parent->parent;

            // Parent is left child of grandparent
            if (parent_pt == grand_parent_pt->left) {

                Node* uncle_pt = grand_parent_pt->right;

                // Case 1: Uncle is RED
                if (uncle_pt != nullptr &&
                    uncle_pt->color == RED) {

                    grand_parent_pt->color = RED;
                    parent_pt->color = BLACK;
                    uncle_pt->color = BLACK;
                    pt = grand_parent_pt;
                }
                else {

                    // Case 2: Left-Right
                    if (pt == parent_pt->right) {
                        rotateLeft(parent_pt);
                        pt = parent_pt;
                        parent_pt = pt->parent;
                    }

                    // Case 3: Left-Left
                    rotateRight(grand_parent_pt);
                    swap(parent_pt->color,
                         grand_parent_pt->color);

                    pt = parent_pt;
                }
            }

            // Parent is right child of grandparent
            else {

                Node* uncle_pt = grand_parent_pt->left;

                // Case 1: Uncle is RED
                if ((uncle_pt != nullptr) &&
                    (uncle_pt->color == RED)) {

                    grand_parent_pt->color = RED;
                    parent_pt->color = BLACK;
                    uncle_pt->color = BLACK;
                    pt = grand_parent_pt;
                }
                else {

                    // Case 2: Right-Left
                    if (pt == parent_pt->left) {
                        rotateRight(parent_pt);
                        pt = parent_pt;
                        parent_pt = pt->parent;
                    }

                    // Case 3: Right-Right
                    rotateLeft(grand_parent_pt);
                    swap(parent_pt->color,
                         grand_parent_pt->color);

                    pt = parent_pt;
                }
            }
        }

        root->color = BLACK;
    }

    void inorderHelper(Node* root) {
        if (root == nullptr)
            return;

        inorderHelper(root->left);

        cout << root->data << " ("
             << (root->color == RED ? "RED" : "BLACK")
             << ") ";

        inorderHelper(root->right);
    }

public:
    RedBlackTree() {
        root = nullptr;
    }

    void insert(int data) {
        Node* pt = new Node(data);

        Node* parent = nullptr;
        Node* current = root;

        while (current != nullptr) {
            parent = current;

            if (pt->data < current->data)
                current = current->left;
            else
                current = current->right;
        }

        pt->parent = parent;

        if (parent == nullptr)
            root = pt;

        else if (pt->data < parent->data)
            parent->left = pt;

        else
            parent->right = pt;

        fixViolation(pt);
    }

    void inorder() {
        inorderHelper(root);
        cout << endl;
    }
};