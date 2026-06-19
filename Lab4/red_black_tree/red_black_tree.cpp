#include <iostream>

enum Color {
    RED,
    BLACK
};

struct Node {
    int data;
    Color color;
    Node* left;
    Node* right;
    Node* parent;

    Node(int value)
        : data(value),
          color(RED),
          left(nullptr),
          right(nullptr),
          parent(nullptr) {}
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

    void rightRotate(Node* y) {
        Node* x = y->left;

        y->left = x->right;

        if (x->right != nullptr)
            x->right->parent = y;

        x->parent = y->parent;

        if (y->parent == nullptr)
            root = x;
        else if (y == y->parent->left)
            y->parent->left = x;
        else
            y->parent->right = x;

        x->right = y;
        y->parent = x;
    }

    void fixViolation(Node* node) {
        while (node != root &&
               node->parent->color == RED) {

            Node* parent = node->parent;
            Node* grandparent = parent->parent;

            if (parent == grandparent->left) {

                Node* uncle = grandparent->right;

                if (uncle != nullptr &&
                    uncle->color == RED) {

                    parent->color = BLACK;
                    uncle->color = BLACK;
                    grandparent->color = RED;

                    node = grandparent;
                }
                else {

                    if (node == parent->right) {
                        node = parent;
                        leftRotate(node);
                    }

                    parent->color = BLACK;
                    grandparent->color = RED;

                    rightRotate(grandparent);
                }
            }
            else {

                Node* uncle = grandparent->left;

                if (uncle != nullptr &&
                    uncle->color == RED) {

                    parent->color = BLACK;
                    uncle->color = BLACK;
                    grandparent->color = RED;

                    node = grandparent;
                }
                else {

                    if (node == parent->left) {
                        node = parent;
                        rightRotate(node);
                    }

                    parent->color = BLACK;
                    grandparent->color = RED;

                    leftRotate(grandparent);
                }
            }
        }

        root->color = BLACK;
    }

    void inorderHelper(Node* node) {
        if (node == nullptr)
            return;

        inorderHelper(node->left);

        std::cout
            << node->data
            << "("
            << (node->color == RED ? "R" : "B")
            << ") ";

        inorderHelper(node->right);
    }

public:
    RedBlackTree() : root(nullptr) {}

    void insert(int value) {

        Node* node = new Node(value);

        Node* parent = nullptr;
        Node* current = root;

        while (current != nullptr) {
            parent = current;

            if (value < current->data)
                current = current->left;
            else
                current = current->right;
        }

        node->parent = parent;

        if (parent == nullptr)
            root = node;
        else if (value < parent->data)
            parent->left = node;
        else
            parent->right = node;

        fixViolation(node);
    }

    void inorder() {
        inorderHelper(root);
        std::cout << std::endl;
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

    std::cout << "Inorder Traversal:\n";
    tree.inorder();

    return 0;
}