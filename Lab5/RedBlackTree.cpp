#include <iostream>
using namespace std;

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

    Node(int value) {
        data = value;
        color = RED;
        left = right = parent = nullptr;
    }
};

class RedBlackTree {
private:
    Node* root;
    Node* NIL;

    void leftRotate(Node* x) {
        Node* y = x->right;

        x->right = y->left;

        if(y->left != NIL)
            y->left->parent = x;

        y->parent = x->parent;

        if(x->parent == nullptr)
            root = y;
        else if(x == x->parent->left)
            x->parent->left = y;
        else
            x->parent->right = y;

        y->left = x;
        x->parent = y;
    }

    void rightRotate(Node* y) {
        Node* x = y->left;

        y->left = x->right;

        if(x->right != NIL)
            x->right->parent = y;

        x->parent = y->parent;

        if(y->parent == nullptr)
            root = x;
        else if(y == y->parent->left)
            y->parent->left = x;
        else
            y->parent->right = x;

        x->right = y;
        y->parent = x;
    }

    void fixInsert(Node* node) {
        while(node->parent && node->parent->color == RED) {

            if(node->parent == node->parent->parent->left) {

                Node* uncle = node->parent->parent->right;

                if(uncle->color == RED) {
                    node->parent->color = BLACK;
                    uncle->color = BLACK;
                    node->parent->parent->color = RED;

                    node = node->parent->parent;
                }
                else {

                    if(node == node->parent->right) {
                        node = node->parent;
                        leftRotate(node);
                    }

                    node->parent->color = BLACK;
                    node->parent->parent->color = RED;

                    rightRotate(node->parent->parent);
                }
            }
            else {

                Node* uncle = node->parent->parent->left;

                if(uncle->color == RED) {
                    node->parent->color = BLACK;
                    uncle->color = BLACK;
                    node->parent->parent->color = RED;

                    node = node->parent->parent;
                }
                else {

                    if(node == node->parent->left) {
                        node = node->parent;
                        rightRotate(node);
                    }

                    node->parent->color = BLACK;
                    node->parent->parent->color = RED;

                    leftRotate(node->parent->parent);
                }
            }
        }

        root->color = BLACK;
    }

    void inorderHelper(Node* node) {
        if(node == NIL)
            return;

        inorderHelper(node->left);

        cout << node->data << "(";

        if(node->color == RED)
            cout << "R";
        else
            cout << "B";

        cout << ") ";

        inorderHelper(node->right);
    }

public:
    RedBlackTree() {
        NIL = new Node(0);

        NIL->color = BLACK;
        NIL->left = NIL;
        NIL->right = NIL;

        root = NIL;
    }

    void insert(int key) {

        Node* node = new Node(key);

        node->left = NIL;
        node->right = NIL;

        Node* parent = nullptr;
        Node* current = root;

        while(current != NIL) {

            parent = current;

            if(key < current->data)
                current = current->left;
            else
                current = current->right;
        }

        node->parent = parent;

        if(parent == nullptr)
            root = node;
        else if(key < parent->data)
            parent->left = node;
        else
            parent->right = node;

        if(node->parent == nullptr) {
            node->color = BLACK;
            return;
        }

        if(node->parent->parent == nullptr)
            return;

        fixInsert(node);
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
    tree.insert(5);
    tree.insert(25);

    cout << "Inorder Traversal:\n";

    tree.inorder();

    return 0;
}