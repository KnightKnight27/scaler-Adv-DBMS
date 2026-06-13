#include <iostream>
using namespace std;

enum Color { RED, BLACK };

class RBTree {
private:
    struct Node {
        int data;
        Color color;
        Node *left, *right, *parent;

        Node(int val)
            : data(val), color(RED),
              left(nullptr), right(nullptr), parent(nullptr) {}
    };

    Node* root = nullptr;

    void rotateLeft(Node* x) {
        Node* y = x->right;
        x->right = y->left;

        if (y->left)
            y->left->parent = x;

        y->parent = x->parent;

        if (!x->parent)
            root = y;
        else if (x == x->parent->left)
            x->parent->left = y;
        else
            x->parent->right = y;

        y->left = x;
        x->parent = y;
    }

    void rotateRight(Node* x) {
        Node* y = x->left;
        x->left = y->right;

        if (y->right)
            y->right->parent = x;

        y->parent = x->parent;

        if (!x->parent)
            root = y;
        else if (x == x->parent->right)
            x->parent->right = y;
        else
            x->parent->left = y;

        y->right = x;
        x->parent = y;
    }

    void fixInsert(Node* node) {
        while (node != root &&
               node->parent->color == RED) {

            Node* parent = node->parent;
            Node* grand = parent->parent;

            if (parent == grand->left) {

                Node* uncle = grand->right;

                if (uncle && uncle->color == RED) {
                    parent->color = BLACK;
                    uncle->color = BLACK;
                    grand->color = RED;
                    node = grand;
                }
                else {
                    if (node == parent->right) {
                        node = parent;
                        rotateLeft(node);
                    }

                    parent->color = BLACK;
                    grand->color = RED;
                    rotateRight(grand);
                }
            }
            else {

                Node* uncle = grand->left;

                if (uncle && uncle->color == RED) {
                    parent->color = BLACK;
                    uncle->color = BLACK;
                    grand->color = RED;
                    node = grand;
                }
                else {
                    if (node == parent->left) {
                        node = parent;
                        rotateRight(node);
                    }

                    parent->color = BLACK;
                    grand->color = RED;
                    rotateLeft(grand);
                }
            }
        }

        root->color = BLACK;
    }

    void inorderTraversal(Node* node) {
        if (!node)
            return;

        inorderTraversal(node->left);

        cout << node->data
             << "("
             << (node->color == RED ? "R" : "B")
             << ") ";

        inorderTraversal(node->right);
    }

public:
    void insert(int value) {
        Node* node = new Node(value);

        Node* parent = nullptr;
        Node* current = root;

        while (current) {
            parent = current;

            if (value < current->data)
                current = current->left;
            else
                current = current->right;
        }

        node->parent = parent;

        if (!parent)
            root = node;
        else if (value < parent->data)
            parent->left = node;
        else
            parent->right = node;

        fixInsert(node);
    }

    void inorder() {
        inorderTraversal(root);
        cout << endl;
    }
};

int main() {

    RBTree tree;

    tree.insert(10);
    tree.insert(20);
    tree.insert(30);
    tree.insert(15);
    tree.insert(5);
    tree.insert(1);

    cout << "Inorder Traversal:\n";
    tree.inorder();

    return 0;
}