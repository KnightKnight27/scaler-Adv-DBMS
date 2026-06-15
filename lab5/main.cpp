#include <iostream>
using namespace std;

struct Node {
    int val;
    bool red;  // true = red, false = black
    Node *left, *right, *parent;

    explicit Node(int value)
        : val(value), red(true), left(nullptr), right(nullptr), parent(nullptr) {}
};

class Tree {
private:
    Node* root = nullptr;

    static bool isRed(Node* node) {
        return node != nullptr && node->red;
    }

    void rotateLeft(Node* x) {
        if (x == nullptr || x->right == nullptr) return;

        Node* y = x->right;
        x->right = y->left;

        if (y->left != nullptr) {
            y->left->parent = x;
        }

        y->parent = x->parent;

        if (x->parent == nullptr) {
            root = y;
        } else if (x == x->parent->left) {
            x->parent->left = y;
        } else {
            x->parent->right = y;
        }

        y->left = x;
        x->parent = y;
    }

    void rotateRight(Node* x) {
        if (x == nullptr || x->left == nullptr) return;

        Node* y = x->left;
        x->left = y->right;

        if (y->right != nullptr) {
            y->right->parent = x;
        }

        y->parent = x->parent;

        if (x->parent == nullptr) {
            root = y;
        } else if (x == x->parent->left) {
            x->parent->left = y;
        } else {
            x->parent->right = y;
        }

        y->right = x;
        x->parent = y;
    }

    void fixInsert(Node* x) {
        while (x != root && x->parent != nullptr && isRed(x->parent)) {
            Node* parent = x->parent;
            Node* grandParent = parent->parent;

            if (grandParent == nullptr) break;

            if (parent == grandParent->left) {
                Node* uncle = grandParent->right;

                if (isRed(uncle)) {
                    parent->red = false;
                    uncle->red = false;
                    grandParent->red = true;
                    x = grandParent;
                } else {
                    if (x == parent->right) {
                        rotateLeft(parent);
                        x = parent;
                        parent = x->parent;
                    }

                    rotateRight(grandParent);
                    parent->red = false;
                    grandParent->red = true;
                }
            } else {
                Node* uncle = grandParent->left;

                if (isRed(uncle)) {
                    parent->red = false;
                    uncle->red = false;
                    grandParent->red = true;
                    x = grandParent;
                } else {
                    if (x == parent->left) {
                        rotateRight(parent);
                        x = parent;
                        parent = x->parent;
                    }

                    rotateLeft(grandParent);
                    parent->red = false;
                    grandParent->red = true;
                }
            }
        }

        if (root != nullptr) {
            root->red = false;
        }
    }
};
