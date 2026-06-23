#include <iostream>

using namespace std;

enum Color { RED, BLACK };

struct RBNode {
    int key;
    Color color;
    RBNode *left, *right, *parent;

    RBNode(int k) {
        key = k;
        color = RED;
        left = right = parent = nullptr;
    }
};

class RedBlackTree {
private:
    RBNode* root = nullptr;

    void leftRotate(RBNode* x) {
        RBNode* y = x->right;

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

    void rightRotate(RBNode* x) {
        RBNode* y = x->left;

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

    void fixInsert(RBNode* z) {
        while (z->parent &&
               z->parent->color == RED) {

            RBNode* gp = z->parent->parent;

            if (z->parent == gp->left) {

                RBNode* uncle = gp->right;

                if (uncle &&
                    uncle->color == RED) {

                    z->parent->color = BLACK;
                    uncle->color = BLACK;
                    gp->color = RED;

                    z = gp;
                }
                else {
                    if (z == z->parent->right) {
                        z = z->parent;
                        leftRotate(z);
                    }

                    z->parent->color = BLACK;
                    gp->color = RED;

                    rightRotate(gp);
                }
            }
            else {

                RBNode* uncle = gp->left;

                if (uncle &&
                    uncle->color == RED) {

                    z->parent->color = BLACK;
                    uncle->color = BLACK;
                    gp->color = RED;

                    z = gp;
                }
                else {

                    if (z == z->parent->left) {
                        z = z->parent;
                        rightRotate(z);
                    }

                    z->parent->color = BLACK;
                    gp->color = RED;

                    leftRotate(gp);
                }
            }
        }

        root->color = BLACK;
    }

    void transplant(RBNode* u, RBNode* v) {
        if (!u->parent)
            root = v;
        else if (u == u->parent->left)
            u->parent->left = v;
        else
            u->parent->right = v;

        if (v)
            v->parent = u->parent;
    }

    RBNode* minimum(RBNode* node) {
        while (node->left)
            node = node->left;

        return node;
    }

    void fixDelete(RBNode* x, RBNode* parent) {

        while (x != root &&
               (!x || x->color == BLACK)) {

            if (x == (parent ? parent->left : nullptr)) {

                RBNode* w = parent->right;

                if (w && w->color == RED) {

                    w->color = BLACK;
                    parent->color = RED;

                    leftRotate(parent);

                    w = parent->right;
                }

                if (w &&
                    (!w->left || w->left->color == BLACK) &&
                    (!w->right || w->right->color == BLACK)) {

                    w->color = RED;

                    x = parent;
                    parent = x->parent;
                }
                else {

                    if (w &&
                        (!w->right ||
                         w->right->color == BLACK)) {

                        if (w->left)
                            w->left->color = BLACK;

                        w->color = RED;

                        rightRotate(w);

                        w = parent->right;
                    }

                    if (w)
                        w->color = parent->color;

                    parent->color = BLACK;

                    if (w && w->right)
                        w->right->color = BLACK;

                    leftRotate(parent);

                    x = root;
                }
            }
            else {

                RBNode* w = parent->left;

                if (w && w->color == RED) {

                    w->color = BLACK;
                    parent->color = RED;

                    rightRotate(parent);

                    w = parent->left;
                }

                if (w &&
                    (!w->left || w->left->color == BLACK) &&
                    (!w->right || w->right->color == BLACK)) {

                    w->color = RED;

                    x = parent;
                    parent = x->parent;
                }
                else {

                    if (w &&
                        (!w->left ||
                         w->left->color == BLACK)) {

                        if (w->right)
                            w->right->color = BLACK;

                        w->color = RED;

                        leftRotate(w);

                        w = parent->left;
                    }

                    if (w)
                        w->color = parent->color;

                    parent->color = BLACK;

                    if (w && w->left)
                        w->left->color = BLACK;

                    rightRotate(parent);

                    x = root;
                }
            }
        }

        if (x)
            x->color = BLACK;
    }

    void inorder(RBNode* node) {
        if (!node)
            return;

        inorder(node->left);

        cout << node->key
             << (node->color == RED ? "R" : "B")
             << " ";

        inorder(node->right);
    }

public:

    void insert(int key) {

        RBNode* z = new RBNode(key);

        RBNode* y = nullptr;
        RBNode* x = root;

        while (x) {
            y = x;

            if (key < x->key)
                x = x->left;
            else
                x = x->right;
        }

        z->parent = y;

        if (!y)
            root = z;
        else if (key < y->key)
            y->left = z;
        else
            y->right = z;

        fixInsert(z);
    }

    void remove(int key) {

        RBNode* z = root;

        while (z && z->key != key) {

            if (key < z->key)
                z = z->left;
            else
                z = z->right;
        }

        if (!z)
            return;

        RBNode* y = z;
        RBNode* x = nullptr;
        RBNode* parent = nullptr;

        Color originalColor = y->color;

        if (!z->left) {

            x = z->right;
            parent = z->parent;

            transplant(z, z->right);
        }
        else if (!z->right) {

            x = z->left;
            parent = z->parent;

            transplant(z, z->left);
        }
        else {

            y = minimum(z->right);

            originalColor = y->color;

            x = y->right;

            if (y->parent == z) {
                parent = y;
            }
            else {

                parent = y->parent;

                transplant(y, y->right);

                y->right = z->right;
                y->right->parent = y;
            }

            transplant(z, y);

            y->left = z->left;
            y->left->parent = y;

            y->color = z->color;
        }

        delete z;

        if (originalColor == BLACK)
            fixDelete(x, parent);
    }

    void print() {
        inorder(root);
        cout << endl;
    }
};

int main() {

    RedBlackTree rbt;

    int values[] = {
        10, 20, 30, 15,
        25, 5, 1
    };

    for (int x : values)
        rbt.insert(x);

    cout << "Inorder (Key + Color):\n";
    rbt.print();

    rbt.remove(20);

    cout << "\nAfter removing 20:\n";
    rbt.print();

    return 0;
}