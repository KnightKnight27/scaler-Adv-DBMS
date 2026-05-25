#include <iostream>
using namespace std;

enum Color { RED, BLACK };

struct Node {
    int data;
    Color color;
    Node *left, *right, *parent;

    Node(int data) {
        this->data = data;
        color = RED;
        left = right = parent = nullptr;
    }
};

class RBTree {
    Node* root;

    void rotateLeft(Node*& pt) {
        Node* pt_right = pt->right;
        pt->right = pt_right->left;

        if (pt->right)
            pt->right->parent = pt;

        pt_right->parent = pt->parent;

        if (!pt->parent)
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

        if (pt->left)
            pt->left->parent = pt;

        pt_left->parent = pt->parent;

        if (!pt->parent)
            root = pt_left;
        else if (pt == pt->parent->left)
            pt->parent->left = pt_left;
        else
            pt->parent->right = pt_left;

        pt_left->right = pt;
        pt->parent = pt_left;
    }

    void fixInsert(Node*& pt) {
        Node* parent = nullptr;
        Node* grandparent = nullptr;

        while (pt != root && pt->color == RED &&
               pt->parent && pt->parent->color == RED) {

            parent = pt->parent;
            grandparent = parent->parent;

            if (parent == grandparent->left) {
                Node* uncle = grandparent->right;

                if (uncle && uncle->color == RED) {
                    grandparent->color = RED;
                    parent->color = BLACK;
                    uncle->color = BLACK;
                    pt = grandparent;
                } else {
                    if (pt == parent->right) {
                        rotateLeft(parent);
                        pt = parent;
                        parent = pt->parent;
                    }

                    rotateRight(grandparent);
                    swap(parent->color, grandparent->color);
                    pt = parent;
                }
            } else {
                Node* uncle = grandparent->left;

                if (uncle && uncle->color == RED) {
                    grandparent->color = RED;
                    parent->color = BLACK;
                    uncle->color = BLACK;
                    pt = grandparent;
                } else {
                    if (pt == parent->left) {
                        rotateRight(parent);
                        pt = parent;
                        parent = pt->parent;
                    }

                    rotateLeft(grandparent);
                    swap(parent->color, grandparent->color);
                    pt = parent;
                }
            }
        }

        root->color = BLACK;
    }

    void inorderHelper(Node* root) {
        if (!root) return;

        inorderHelper(root->left);
        cout << root->data << "("
             << (root->color == RED ? "R" : "B") << ") ";
        inorderHelper(root->right);
    }

public:
    RBTree() { root = nullptr; }

    void insert(int data) {
        Node* pt = new Node(data);
        Node* parent = nullptr;
        Node* curr = root;

        while (curr) {
            parent = curr;
            if (pt->data < curr->data)
                curr = curr->left;
            else
                curr = curr->right;
        }

        pt->parent = parent;

        if (!parent)
            root = pt;
        else if (pt->data < parent->data)
            parent->left = pt;
        else
            parent->right = pt;

        fixInsert(pt);
    }

    void inorder() {
        inorderHelper(root);
        cout << endl;
    }
};

int main() {
    RBTree tree;

    tree.insert(10);
    tree.insert(20);
    tree.insert(30);
    tree.insert(15);
    tree.insert(25);
    tree.insert(5);

    cout << "Red-Black Tree Inorder Traversal:" << endl;
    tree.inorder();

    return 0;
}