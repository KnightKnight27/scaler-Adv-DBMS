#include <iostream>
#include <algorithm>
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

class RedBlackTree {
private:
    Node* root;

    void rotateLeft(Node*& root, Node*& pt) {
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

    void rotateRight(Node*& root, Node*& pt) {
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

    void fixViolation(Node*& root, Node*& pt) {
        Node *parent_pt = nullptr;
        Node *grand_parent_pt = nullptr;

        while ((pt != root) && (pt->color != BLACK) &&
               (pt->parent->color == RED)) {

            parent_pt = pt->parent;
            grand_parent_pt = pt->parent->parent;

            // Parent is left child
            if (parent_pt == grand_parent_pt->left) {

                Node* uncle_pt = grand_parent_pt->right;

                // Uncle is red
                if (uncle_pt != nullptr && uncle_pt->color == RED) {
                    grand_parent_pt->color = RED;
                    parent_pt->color = BLACK;
                    uncle_pt->color = BLACK;
                    pt = grand_parent_pt;
                }

                else {
                    // Left Right Case
                    if (pt == parent_pt->right) {
                        rotateLeft(root, parent_pt);
                        pt = parent_pt;
                        parent_pt = pt->parent;
                    }

                    // Left Left Case
                    rotateRight(root, grand_parent_pt);
                    sstd::swap(parent_pt->color, grand_parent_pt->color);
                    pt = parent_pt;
                }
            }

            // Parent is right child
            else {
                Node* uncle_pt = grand_parent_pt->left;

                // Uncle is red
                if ((uncle_pt != nullptr) && (uncle_pt->color == RED)) {
                    grand_parent_pt->color = RED;
                    parent_pt->color = BLACK;
                    uncle_pt->color = BLACK;
                    pt = grand_parent_pt;
                }

                else {
                    // Right Left Case
                    if (pt == parent_pt->left) {
                        rotateRight(root, parent_pt);
                        pt = parent_pt;
                        parent_pt = pt->parent;
                    }

                    // Right Right Case
                    rotateLeft(root, grand_parent_pt);
                    std::swap(parent_pt->color, grand_parent_pt->color);
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

    Node* searchHelper(Node* root, int key) {
        if (root == nullptr || root->data == key)
            return root;

        if (key < root->data)
            return searchHelper(root->left, key);

        return searchHelper(root->right, key);
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

        fixViolation(root, pt);
    }

    void inorder() {
        inorderHelper(root);
        cout << endl;
    }

    void search(int key) {
        Node* result = searchHelper(root, key);

        if (result != nullptr)
            cout << "Element " << key << " found in tree." << endl;
        else
            cout << "Element " << key << " not found in tree." << endl;
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

    cout << "Inorder Traversal of Red-Black Tree:" << endl;
    tree.inorder();

    tree.search(15);
    tree.search(100);

    return 0;
}