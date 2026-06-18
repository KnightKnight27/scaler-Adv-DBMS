#include <iostream>
using namespace std;

enum Color { RED, BLACK };

class Node {
public:
    int data;
    Color color;
    Node *left, *right, *parent;

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

        if (y->left != NIL)
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

    void rightRotate(Node* x) {
        Node* y = x->left;
        x->left = y->right;

        if (y->right != NIL)
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

    void fixInsert(Node* k) {
        Node* u;

        while (k->parent && k->parent->color == RED) {

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

            if (k == root)
                break;
        }

        root->color = BLACK;
    }

    void transplant(Node* u, Node* v) {
        if (u->parent == nullptr)
            root = v;
        else if (u == u->parent->left)
            u->parent->left = v;
        else
            u->parent->right = v;

        v->parent = u->parent;
    }

    Node* minimum(Node* node) {
        while (node->left != NIL)
            node = node->left;

        return node;
    }

    void fixDelete(Node* x) {
        Node* s;

        while (x != root && x->color == BLACK) {

            if (x == x->parent->left) {
                s = x->parent->right;

                if (s->color == RED) {
                    s->color = BLACK;
                    x->parent->color = RED;
                    leftRotate(x->parent);
                    s = x->parent->right;
                }

                if (s->left->color == BLACK &&
                    s->right->color == BLACK) {

                    s->color = RED;
                    x = x->parent;

                } else {

                    if (s->right->color == BLACK) {
                        s->left->color = BLACK;
                        s->color = RED;
                        rightRotate(s);
                        s = x->parent->right;
                    }

                    s->color = x->parent->color;
                    x->parent->color = BLACK;
                    s->right->color = BLACK;
                    leftRotate(x->parent);
                    x = root;
                }

            } else {

                s = x->parent->left;

                if (s->color == RED) {
                    s->color = BLACK;
                    x->parent->color = RED;
                    rightRotate(x->parent);
                    s = x->parent->left;
                }

                if (s->right->color == BLACK &&
                    s->left->color == BLACK) {

                    s->color = RED;
                    x = x->parent;

                } else {

                    if (s->left->color == BLACK) {
                        s->right->color = BLACK;
                        s->color = RED;
                        leftRotate(s);
                        s = x->parent->left;
                    }

                    s->color = x->parent->color;
                    x->parent->color = BLACK;
                    s->left->color = BLACK;
                    rightRotate(x->parent);
                    x = root;
                }
            }
        }

        x->color = BLACK;
    }

public:
    RedBlackTree() {
        NIL = new Node(0);
        NIL->color = BLACK;
        root = NIL;
    }

    void insert(int key) {
        Node* node = new Node(key);

        node->parent = nullptr;
        node->left = NIL;
        node->right = NIL;

        Node* y = nullptr;
        Node* x = root;

        while (x != NIL) {
            y = x;

            if (node->data < x->data)
                x = x->left;
            else
                x = x->right;
        }

        node->parent = y;

        if (y == nullptr)
            root = node;
        else if (node->data < y->data)
            y->left = node;
        else
            y->right = node;

        if (node->parent == nullptr) {
            node->color = BLACK;
            return;
        }

        if (node->parent->parent == nullptr)
            return;

        fixInsert(node);
    }

    bool search(int key) {
        Node* temp = root;

        while (temp != NIL) {
            if (key == temp->data)
                return true;

            if (key < temp->data)
                temp = temp->left;
            else
                temp = temp->right;
        }

        return false;
    }

    void deleteNode(int key) {
        Node* z = root;
        Node* x;
        Node* y;

        while (z != NIL) {

            if (z->data == key)
                break;

            if (key < z->data)
                z = z->left;
            else
                z = z->right;
        }

        if (z == NIL) {
            cout << "Node not found\n";
            return;
        }

        y = z;
        Color yOriginalColor = y->color;

        if (z->left == NIL) {
            x = z->right;
            transplant(z, z->right);

        } else if (z->right == NIL) {
            x = z->left;
            transplant(z, z->left);

        } else {
            y = minimum(z->right);
            yOriginalColor = y->color;
            x = y->right;

            if (y->parent == z)
                x->parent = y;
            else {
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

        if (yOriginalColor == BLACK)
            fixDelete(x);
    }

    void inorder(Node* node) {
        if (node != NIL) {
            inorder(node->left);
            cout << node->data << " ";
            inorder(node->right);
        }
    }

    void display() {
        inorder(root);
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

    cout << "Tree elements: ";
    tree.display();

    if (tree.search(15))
        cout << "15 Found\n";
    else
        cout << "15 Not Found\n";

    tree.deleteNode(20);

    cout << "After deleting 20: ";
    tree.display();

    return 0;
}