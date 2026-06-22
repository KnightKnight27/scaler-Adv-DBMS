#include <iostream>
using namespace std;

class Node {
public:
    int data;
    bool color; // 1 -> Red, 0 -> Black
    Node *left, *right, *parent;

    Node(int val) {
        data = val;
        color = 1; // new node is always red
        left = right = parent = nullptr;
    }
};

class RedBlackTree {
private:
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

    void fixViolation(Node* node) {
        while (node != root && node->parent->color == 1) {
            Node* par = node->parent;
            Node* gpar = par->parent;

            // parent is left child of grandparent
            if (par == gpar->left) {
                Node* uncle = gpar->right;

                // Case 1: uncle is red -> recolor
                if (uncle != nullptr && uncle->color == 1) {
                    par->color = 0;
                    uncle->color = 0;
                    gpar->color = 1;
                    node = gpar;
                }
                else {
                    // Case 2: node is right child -> left rotate
                    if (node == par->right) {
                        node = par;
                        rotateLeft(node);
                        par = node->parent;
                    }

                    // Case 3: node is left child -> right rotate
                    par->color = 0;
                    gpar->color = 1;
                    rotateRight(gpar);
                    break;
                }
            }
            else {
                // parent is right child of grandparent (mirror)
                Node* uncle = gpar->left;

                if (uncle != nullptr && uncle->color == 1) {
                    par->color = 0;
                    uncle->color = 0;
                    gpar->color = 1;
                    node = gpar;
                }
                else {
                    if (node == par->left) {
                        node = par;
                        rotateRight(node);
                        par = node->parent;
                    }

                    par->color = 0;
                    gpar->color = 1;
                    rotateLeft(gpar);
                    break;
                }
            }
        }

        root->color = 0; // root is always black
    }

    void inorderHelper(Node* node) {
        if (node == nullptr)
            return;

        inorderHelper(node->left);
        cout << node->data << (node->color ? "(R) " : "(B) ");
        inorderHelper(node->right);
    }

public:
    RedBlackTree() {
        root = nullptr;
    }

    void insert(int val) {
        Node* newNode = new Node(val);

        Node* par = nullptr;
        Node* curr = root;

        while (curr != nullptr) {
            par = curr;
            if (val < curr->data)
                curr = curr->left;
            else
                curr = curr->right;
        }

        newNode->parent = par;

        if (par == nullptr)
            root = newNode;
        else if (val < par->data)
            par->left = newNode;
        else
            par->right = newNode;

        fixViolation(newNode);
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

    cout << "Inorder Traversal of Red-Black Tree:" << endl;
    tree.inorder();

    return 0;
}
