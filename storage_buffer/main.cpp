#include <iostream>
#include <queue>
using namespace std;

template<typename T>
class RedBlackTree {
private:
    enum Color {
        RED,
        BLACK
    };

    struct Node {
        T data;
        Color color;

        Node* left;
        Node* right;
        Node* parent;

        Node(T value)
            : data(value),
              color(RED),
              left(nullptr),
              right(nullptr),
              parent(nullptr) {}
    };

    Node* root;

private:
    void leftRotate(Node* x) {
        Node* y = x->right;

        x->right = y->left;

        if (y->left != nullptr) {
            y->left->parent = x;
        }

        y->parent = x->parent;

        if (x->parent == nullptr) {
            root = y;
        }
        else if (x == x->parent->left) {
            x->parent->left = y;
        }
        else {
            x->parent->right = y;
        }

        y->left = x;
        x->parent = y;
    }

    void rightRotate(Node* y) {
        Node* x = y->left;

        y->left = x->right;

        if (x->right != nullptr) {
            x->right->parent = y;
        }

        x->parent = y->parent;

        if (y->parent == nullptr) {
            root = x;
        }
        else if (y == y->parent->left) {
            y->parent->left = x;
        }
        else {
            y->parent->right = x;
        }

        x->right = y;
        y->parent = x;
    }

    void fixInsert(Node* node) {
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
                }
                else {
                    if (node == parent->right) {
                        node = parent;
                        leftRotate(node);

                        parent = node->parent;
                        grandparent = parent->parent;
                    }

                    parent->color = BLACK;
                    grandparent->color = RED;

                    rightRotate(grandparent);
                }
            }
            else {

                Node* uncle = grandparent->left;

                if (uncle != nullptr && uncle->color == RED) {
                    parent->color = BLACK;
                    uncle->color = BLACK;
                    grandparent->color = RED;

                    node = grandparent;
                }
                else {
                    if (node == parent->left) {
                        node = parent;
                        rightRotate(node);

                        parent = node->parent;
                        grandparent = parent->parent;
                    }

                    parent->color = BLACK;
                    grandparent->color = RED;

                    leftRotate(grandparent);
                }
            }
        }

        root->color = BLACK;
    }

    void destroy(Node* node) {
        if (node == nullptr) {
            return;
        }

        destroy(node->left);
        destroy(node->right);

        delete node;
    }

public:
    RedBlackTree()
        : root(nullptr) {}

    ~RedBlackTree() {
        destroy(root);
    }

    void insert(T value) {
        Node* newNode = new Node(value);

        Node* parent = nullptr;
        Node* current = root;

        while (current != nullptr) {
            parent = current;

            if (value < current->data) {
                current = current->left;
            }
            else {
                current = current->right;
            }
        }

        newNode->parent = parent;

        if (parent == nullptr) {
            root = newNode;
        }
        else if (value < parent->data) {
            parent->left = newNode;
        }
        else {
            parent->right = newNode;
        }

        if (newNode == root) {
            newNode->color = BLACK;
            return;
        }

        fixInsert(newNode);
    }

    bool search(T value) const {
        Node* current = root;

        while (current != nullptr) {

            if (value == current->data) {
                return true;
            }
            else if (value < current->data) {
                current = current->left;
            }
            else {
                current = current->right;
            }
        }

        return false;
    }
};

int main() {

    RedBlackTree<int> tree;

    tree.insert(10);
    tree.insert(20);
    tree.insert(30);
    tree.insert(15);
    tree.insert(25);
    tree.insert(5);
    tree.insert(1);

    cout << "Search 15: "
         << (tree.search(15) ? "Found" : "Not Found")
         << '\n';

    cout << "Search 100: "
         << (tree.search(100) ? "Found" : "Not Found")
         << '\n';

    return 0;
}
