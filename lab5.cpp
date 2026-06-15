#include <iostream>
using namespace std;

enum Color { RED, BLACK };

template <typename T>
class RedBlackTree {
private:
    struct Node {
        T key;
        Color color;
        Node* left;
        Node* right;
        Node* parent;

        Node(T value) : key(value), color(RED), left(nullptr), right(nullptr), parent(nullptr) {}
    };

    Node* root;

    void rotateLeft(Node* x) {
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
        Node* y = x->left;
        x->left = y->right;

        if (y->right != nullptr) {
            y->right->parent = x;
        }

        y->parent = x->parent;

        if (x->parent == nullptr) {
            root = y;
        } else if (x == x->parent->right) {
            x->parent->right = y;
        } else {
            x->parent->left = y;
        }

        y->right = x;
        x->parent = y;
    }

    void bstInsert(Node* z) {
        Node* y = nullptr;
        Node* x = root;

        while (x != nullptr) {
            y = x;
            if (z->key < x->key) {
                x = x->left;
            } else {
                x = x->right;
            }
        }

        z->parent = y;

        if (y == nullptr) {
            root = z;
        } else if (z->key < y->key) {
            y->left = z;
        } else {
            y->right = z;
        }
    }

    void fixViolation(Node* z) {
        while (z != root && z->parent != nullptr && z->parent->color == RED) {
            Node* p = z->parent;
            Node* g = p->parent;

            if (g == nullptr) break;

            if (p == g->left) {
                Node* u = g->right;

                if (u != nullptr && u->color == RED) {
                    p->color = BLACK;
                    u->color = BLACK;
                    g->color = RED;
                    z = g;
                } else {
                    if (z == p->right) {
                        z = p;
                        rotateLeft(z);
                        p = z->parent;
                        g = p ? p->parent : nullptr;
                    }

                    if (p != nullptr) p->color = BLACK;
                    if (g != nullptr) {
                        g->color = RED;
                        rotateRight(g);
                    }
                }
            } else {
                Node* u = g->left;

                if (u != nullptr && u->color == RED) {
                    p->color = BLACK;
                    u->color = BLACK;
                    g->color = RED;
                    z = g;
                } else {
                    if (z == p->left) {
                        z = p;
                        rotateRight(z);
                        p = z->parent;
                        g = p ? p->parent : nullptr;
                    }

                    if (p != nullptr) p->color = BLACK;
                    if (g != nullptr) {
                        g->color = RED;
                        rotateLeft(g);
                    }
                }
            }
        }

        if (root != nullptr) {
            root->color = BLACK;
        }
    }

    void inorder(Node* node) const {
        if (node == nullptr) return;

        inorder(node->left);
        cout << node->key << "(" << (node->color == RED ? "R" : "B") << ") ";
        inorder(node->right);
    }

    void clear(Node* node) {
        if (node == nullptr) return;
        clear(node->left);
        clear(node->right);
        delete node;
    }

public:
    RedBlackTree() : root(nullptr) {}

    ~RedBlackTree() {
        clear(root);
    }

    void insert(T value) {
        Node* node = new Node(value);
        bstInsert(node);
        fixViolation(node);
    }

    void printInorder() const {
        inorder(root);
        cout << endl;
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

    cout << "Inorder Traversal:\n";
    tree.printInorder();

    return 0;
}
