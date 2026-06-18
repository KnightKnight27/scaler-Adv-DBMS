#include <iostream>
using namespace std;

class Node {
public:
    int data;
    bool color; // 1 -> Red, 0 -> Black
    Node *left, *right, *parent;

    Node(int value) {
        data = value;
        color = 1; // New node is always red
        left = right = parent = nullptr;
    }
};

class RedBlackTree {
private:
    Node* root;

    void leftRotate(Node* x) {
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

    void rightRotate(Node* x) {
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

    void fixInsert(Node* k) {
        while (k != root && k->parent->color == 1) {
            Node* parent = k->parent;
            Node* grandparent = parent->parent;

            // Parent is left child
            if (parent == grandparent->left) {
                Node* uncle = grandparent->right;

                // Case 1: Uncle is red
                if (uncle != nullptr && uncle->color == 1) {
                    parent->color = 0;
                    uncle->color = 0;
                    grandparent->color = 1;
                    k = grandparent;
                }
                else {
                    // Case 2: Left-Right
                    if (k == parent->right) {
                        k = parent;
                        leftRotate(k);
                        parent = k->parent;
                    }

                    // Case 3: Left-Left
                    parent->color = 0;
                    grandparent->color = 1;
                    rightRotate(grandparent);
                    break;
                }
            }
            else {
                Node* uncle = grandparent->left;

                // Case 1: Uncle is red
                if (uncle != nullptr && uncle->color == 1) {
                    parent->color = 0;
                    uncle->color = 0;
                    grandparent->color = 1;
                    k = grandparent;
                }
                else {
                    // Case 2: Right-Left
                    if (k == parent->left) {
                        k = parent;
                        rightRotate(k);
                        parent = k->parent;
                    }

                    // Case 3: Right-Right
                    parent->color = 0;
                    grandparent->color = 1;
                    leftRotate(grandparent);
                    break;
                }
            }
        }

        root->color = 0; // Root must always be black
    }

    void inorderHelper(Node* node) {
        if (node == nullptr)
            return;

        inorderHelper(node->left);

        cout << node->data << " ";
        if (node->color)
            cout << "(R) ";
        else
            cout << "(B) ";

        inorderHelper(node->right);
    }

public:
    RedBlackTree() {
        root = nullptr;
    }

    void insert(int value) {
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

    void inorder() {
        inorderHelper(root);
        cout << endl;
    }
};

int main() {
    RedBlackTree tree;

    tree.insert(10);
    tree.insert(20);
    tree.insert(30);
    tree.insert(15);
    tree.insert(25);
    tree.insert(5);

    cout << "Inorder Traversal of Red-Black Tree:\n";
    tree.inorder();

    return 0;
}