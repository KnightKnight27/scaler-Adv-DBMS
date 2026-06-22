#include <iostream>
using namespace std;

enum Color { RED, BLACK };

struct Node {
    int data;
    Color color;
    Node *left, *right, *parent;

    Node(int val) {
        data = val;
        color = RED;
        left = right = parent = nullptr;
    }
};

class RedBlackTree {
private:
    Node* root;

    // Left Rotation
    void rotateLeft(Node* x) {
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

    // Right Rotation
    void rotateRight(Node* x) {
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

    // Fix Red-Black Tree after insertion
    void fixInsert(Node* k) {
        while (k != root && k->parent->color == RED) {

            // Parent is left child
            if (k->parent == k->parent->parent->left) {
                Node* uncle = k->parent->parent->right;

                // Case 1: Uncle is RED
                if (uncle != nullptr && uncle->color == RED) {
                    k->parent->color = BLACK;
                    uncle->color = BLACK;
                    k->parent->parent->color = RED;
                    k = k->parent->parent;
                }
                else {
                    // Case 2: Triangle
                    if (k == k->parent->right) {
                        k = k->parent;
                        rotateLeft(k);
                    }

                    // Case 3: Line
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    rotateRight(k->parent->parent);
                }
            }

            // Parent is right child
            else {
                Node* uncle = k->parent->parent->left;

                // Case 1
                if (uncle != nullptr && uncle->color == RED) {
                    k->parent->color = BLACK;
                    uncle->color = BLACK;
                    k->parent->parent->color = RED;
                    k = k->parent->parent;
                }
                else {
                    // Case 2
                    if (k == k->parent->left) {
                        k = k->parent;
                        rotateRight(k);
                    }

                    // Case 3
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    rotateLeft(k->parent->parent);
                }
            }
        }

        root->color = BLACK;
    }

    // Inorder Traversal
    void inorder(Node* node) {
        if (node == nullptr)
            return;

        inorder(node->left);

        cout << node->data << " ";
        if (node->color == RED)
            cout << "(R) ";
        else
            cout << "(B) ";

        inorder(node->right);
    }

public:
    RedBlackTree() {
        root = nullptr;
    }

    // Insert Node
    void insert(int val) {
        Node* newNode = new Node(val);

        Node* y = nullptr;
        Node* x = root;

        // BST insertion
        while (x != nullptr) {
            y = x;

            if (newNode->data < x->data)
                x = x->left;
            else
                x = x->right;
        }

        newNode->parent = y;

        if (y == nullptr)
            root = newNode;
        else if (newNode->data < y->data)
            y->left = newNode;
        else
            y->right = newNode;

        // Fix violations
        fixInsert(newNode);
    }

    void display() {
        inorder(root);
        cout << endl;
    }
};
