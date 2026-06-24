#include <iostream>
#include <string>

using namespace std;

enum NodeColor { BLACK, RED };

struct RBTNode {
    int data;
    RBTNode* parent;
    RBTNode* left;
    RBTNode* right;
    NodeColor color;
};

class RedBlackTree {
private:
    RBTNode* root;
    RBTNode* TNULL;

    RBTNode* createNode(int data) {
        RBTNode* node = new RBTNode;
        node->data = data;
        node->parent = nullptr;
        node->left = TNULL;
        node->right = TNULL;
        node->color = RED;
        return node;
    }

    void leftRotate(RBTNode* x) {
        RBTNode* y = x->right;
        x->right = y->left;

        if (y->left != TNULL) {
            y->left->parent = x;
        }

        y->parent = x->parent;

        if (x->parent == nullptr) {
            this->root = y;
        } else if (x == x->parent->left) {
            x->parent->left = y;
        } else {
            x->parent->right = y;
        }

        y->left = x;
        x->parent = y;
    }

    void rightRotate(RBTNode* x) {
        RBTNode* y = x->left;
        x->left = y->right;

        if (y->right != TNULL) {
            y->right->parent = x;
        }

        y->parent = x->parent;

        if (x->parent == nullptr) {
            this->root = y;
        } else if (x == x->parent->right) {
            x->parent->right = y;
        } else {
            x->parent->left = y;
        }

        y->right = x;
        x->parent = y;
    }

    void insertFixup(RBTNode* k) {
        RBTNode* u;

        while (k->parent != nullptr && k->parent->color == RED) {
            if (k->parent == k->parent->parent->right) {
                u = k->parent->parent->left;

                if (u->color == RED) {
                    u->color = BLACK;
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    k = k->parent->parent;
                } else {
                    if (k == k->parent->left) {
                        k = k->parent;
                        rightRotate(k);
                    }

                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    leftRotate(k->parent->parent);
                }
            } else {
                u = k->parent->parent->right;

                if (u->color == RED) {
                    u->color = BLACK;
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    k = k->parent->parent;
                } else {
                    if (k == k->parent->right) {
                        k = k->parent;
                        leftRotate(k);
                    }

                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    rightRotate(k->parent->parent);
                }
            }

            if (k == root) {
                break;
            }
        }

        root->color = BLACK;
    }

    void printHelper(RBTNode* root, string indent, bool last) {
        if (root != TNULL) {
            cout << indent;

            if (last) {
                cout << "R----";
                indent += "     ";
            } else {
                cout << "L----";
                indent += "|    ";
            }

            string sColor = root->color ? "RED" : "BLACK";

            cout << root->data << " (" << sColor << ")" << endl;

            printHelper(root->left, indent, false);
            printHelper(root->right, indent, true);
        }
    }

public:
    RedBlackTree() {
        TNULL = new RBTNode;
        TNULL->color = BLACK;
        TNULL->left = nullptr;
        TNULL->right = nullptr;
        root = TNULL;
    }

    void insert(int key) {
        RBTNode* node = createNode(key);
        RBTNode* y = nullptr;
        RBTNode* x = this->root;

        while (x != TNULL) {
            y = x;

            if (node->data < x->data) {
                x = x->left;
            } else {
                x = x->right;
            }
        }

        node->parent = y;

        if (y == nullptr) {
            root = node;
        } else if (node->data < y->data) {
            y->left = node;
        } else {
            y->right = node;
        }

        if (node->parent == nullptr) {
            node->color = BLACK;
            return;
        }

        if (node->parent->parent == nullptr) {
            return;
        }

        insertFixup(node);
    }

    void display() {
        if (root == TNULL) {
            cout << "Tree is empty." << endl;
        } else {
            printHelper(this->root, "", true);
        }
    }
};

int main() {
    RedBlackTree rbt;

    cout << "Inserting elements: 55, 40, 65, 60, 75, 57" << endl;

    rbt.insert(55);
    rbt.insert(40);
    rbt.insert(65);
    rbt.insert(60);
    rbt.insert(75);
    rbt.insert(57);

    cout << "\nRed-Black Tree Structure:" << endl;

    rbt.display();

    return 0;
}