#include <iostream>
using namespace std;

enum Color { RED, BLACK };

template <typename T>
class RedBlackTree {
private:
    struct Node {
        T data;
        Color color;
        Node* left;
        Node* right;
        Node* parent;

        Node(T value) {
            data = value;
            color = RED;
            left = right = parent = nullptr;
        }
    };

    Node* root;

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

    void rotateRight(Node* y) {
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

    void fixInsert(Node* k) {
        while (k != root && k->parent->color == RED) {
            Node* parent = k->parent;
            Node* grandparent = parent->parent;

            if (parent == grandparent->left) {
                Node* uncle = grandparent->right;

                if (uncle != nullptr && uncle->color == RED) {
                    parent->color = BLACK;
                    uncle->color = BLACK;
                    grandparent->color = RED;
                    k = grandparent;
                }
                else {
                    if (k == parent->right) {
                        k = parent;
                        rotateLeft(k);
                    }

                    parent->color = BLACK;
                    grandparent->color = RED;
                    rotateRight(grandparent);
                }
            }
            else {
                Node* uncle = grandparent->left;

                if (uncle != nullptr && uncle->color == RED) {
                    parent->color = BLACK;
                    uncle->color = BLACK;
                    grandparent->color = RED;
                    k = grandparent;
                }
                else {
                    if (k == parent->left) {
                        k = parent;
                        rotateRight(k);
                    }

                    parent->color = BLACK;
                    grandparent->color = RED;
                    rotateLeft(grandparent);
                }
            }
        }

        root->color = BLACK;
    }

    void bstInsert(Node* node) {
        Node* parent = nullptr;
        Node* current = root;

        while (current != nullptr) {
            parent = current;

            if (node->data < current->data)
                current = current->left;
            else
                current = current->right;
        }

        node->parent = parent;

        if (parent == nullptr)
            root = node;
        else if (node->data < parent->data)
            parent->left = node;
        else
            parent->right = node;
    }

    void inorder(Node* node) {
        if (node == nullptr)
            return;

        inorder(node->left);
        cout << node->data << "(" << (node->color == RED ? "R" : "B") << ") ";
        inorder(node->right);
    }

public:
    RedBlackTree() {
        root = nullptr;
    }

    void insert(T value) {
        Node* node = new Node(value);
        bstInsert(node);
        fixInsert(node);
    }

    void inorderTraversal() {
        inorder(root);
        cout << endl;
    }
};

int main() {
    RedBlackTree<int> rbt;

    rbt.insert(10);
    rbt.insert(20);
    rbt.insert(30);
    rbt.insert(15);
    rbt.insert(25);
    rbt.insert(5);

    cout << "Inorder Traversal:\n";
    rbt.inorderTraversal();

    return 0;
}
