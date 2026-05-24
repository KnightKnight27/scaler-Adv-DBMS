#ifndef REDBLACKTREE_HPP
#define REDBLACKTREE_HPP

#include <iostream>
#include <algorithm>
#include <string>
#include <vector>

enum Color { RED, BLACK };

template <typename T>
struct Node {
    T data;
    Color color;
    Node *left, *right, *parent;

    Node(T val) : data(val), color(RED), left(nullptr), right(nullptr), parent(nullptr) {}
};

template <typename T>
class RedBlackTree {
private:
    Node<T>* root;
    Node<T>* TNULL;

    void leftRotate(Node<T>* x) {
        Node<T>* y = x->right;
        x->right = y->left;
        if (y->left != TNULL) {
            y->left->parent = x;
        }
        y->parent = x->parent;
        if (x->parent == TNULL) {
            root = y;
        } else if (x == x->parent->left) {
            x->parent->left = y;
        } else {
            x->parent->right = y;
        }
        y->left = x;
        x->parent = y;
    }

    void rightRotate(Node<T>* x) {
        Node<T>* y = x->left;
        x->left = y->right;
        if (y->right != TNULL) {
            y->right->parent = x;
        }
        y->parent = x->parent;
        if (x->parent == TNULL) {
            root = y;
        } else if (x == x->parent->right) {
            x->parent->right = y;
        } else {
            x->parent->left = y;
        }
        y->right = x;
        x->parent = y;
    }

    void fixInsert(Node<T>* k) {
        Node<T>* u;
        while (k->parent->color == RED) {
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
            if (k == root) break;
        }
        root->color = BLACK;
    }

    void fixDelete(Node<T>* x) {
        Node<T>* s;
        while (x != root && x->color == BLACK) {
            if (x == x->parent->left) {
                s = x->parent->right;
                if (s->color == RED) {
                    s->color = BLACK;
                    x->parent->color = RED;
                    leftRotate(x->parent);
                    s = x->parent->right;
                }

                if (s->left->color == BLACK && s->right->color == BLACK) {
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

                if (s->right->color == BLACK && s->left->color == BLACK) {
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

    void transplant(Node<T>* u, Node<T>* v) {
        if (u->parent == TNULL) {
            root = v;
        } else if (u == u->parent->left) {
            u->parent->left = v;
        } else {
            u->parent->right = v;
        }
        v->parent = u->parent;
    }

    void deleteNodeHelper(Node<T>* node, T key) {
        Node<T>* z = TNULL;
        Node<T>* x, *y;

        Node<T>* curr = root;
        while (curr != TNULL) {
            if (curr->data == key) {
                z = curr;
                break;
            }
            if (curr->data <= key) {
                curr = curr->right;
            } else {
                curr = curr->left;
            }
        }

        if (z == TNULL) {
            std::cout << "Value " << key << " not found in tree." << std::endl;
            return;
        }

        y = z;
        Color yOriginalColor = y->color;

        if (z->left == TNULL) {
            x = z->right;
            transplant(z, z->right);
        } else if (z->right == TNULL) {
            x = z->left;
            transplant(z, z->left);
        } else {
            y = minimum(z->right);
            yOriginalColor = y->color;
            x = y->right;
            if (y->parent == z) {
                x->parent = y;
            } else {
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

        if (yOriginalColor == BLACK) {
            fixDelete(x);
        }
    }

    void inorderHelper(Node<T>* node) {
        if (node != TNULL) {
            inorderHelper(node->left);
            std::cout << node->data << " ";
            inorderHelper(node->right);
        }
    }

    void preorderHelper(Node<T>* node) {
        if (node != TNULL) {
            std::cout << node->data << " ";
            preorderHelper(node->left);
            preorderHelper(node->right);
        }
    }

    void postorderHelper(Node<T>* node) {
        if (node != TNULL) {
            postorderHelper(node->left);
            postorderHelper(node->right);
            std::cout << node->data << " ";
        }
    }

    Node<T>* minimum(Node<T>* node) {
        while (node->left != TNULL) {
            node = node->left;
        }
        return node;
    }

    Node<T>* maximum(Node<T>* node) {
        while (node->right != TNULL) {
            node = node->right;
        }
        return node;
    }

    Node<T>* searchTreeHelper(Node<T>* node, T key) {
        if (node == TNULL || key == node->data) {
            return node;
        }

        if (key < node->data) {
            return searchTreeHelper(node->left, key);
        }
        return searchTreeHelper(node->right, key);
    }

    bool isRBUtil(Node<T>* node, int bh, int& expectedBH) {
        if (node == TNULL) {
            if (expectedBH == -1) {
                expectedBH = bh;
                return true;
            }
            return bh == expectedBH;
        }

        if (node->color == RED && node->parent->color == RED) {
            return false;
        }

        if (node->color == BLACK) {
            bh++;
        }

        return isRBUtil(node->left, bh, expectedBH) &&
               isRBUtil(node->right, bh, expectedBH);
    }

    void printTreeHelper(Node<T>* node, std::string indent, bool last) {
        if (node == TNULL) return;
        std::cout << indent;
        if (last) {
            std::cout << "R----";
            indent += "     ";
        } else {
            std::cout << "L----";
            indent += "|    ";
        }
        std::string color = node->color == RED ? "RED" : "BLACK";
        std::cout << node->data << " (" << color << ")" << std::endl;
        printTreeHelper(node->left, indent, false);
        printTreeHelper(node->right, indent, true);
    }

    void deleteTree(Node<T>* node) {
        if (node != TNULL) {
            deleteTree(node->left);
            deleteTree(node->right);
            delete node;
        }
    }

public:
    RedBlackTree() {
        TNULL = new Node<T>(T());
        TNULL->color = BLACK;
        root = TNULL;
    }

    ~RedBlackTree() {
        deleteTree(root);
        delete TNULL;
    }

    void insert(T key) {
        Node<T>* node = new Node<T>(key);
        node->parent = TNULL;
        node->left = TNULL;
        node->right = TNULL;

        Node<T>* y = TNULL;
        Node<T>* x = root;

        while (x != TNULL) {
            y = x;
            if (node->data < x->data) {
                x = x->left;
            } else {
                x = x->right;
            }
        }

        node->parent = y;
        if (y == TNULL) {
            root = node;
        } else if (node->data < y->data) {
            y->left = node;
        } else {
            y->right = node;
        }

        if (root == node) {
            node->color = BLACK;
            return;
        }

        fixInsert(node);
    }

    void remove(T key) {
        deleteNodeHelper(root, key);
    }

    bool search(T key) {
        return searchTreeHelper(root, key) != TNULL;
    }

    void inorder() {
        std::cout << "Inorder:   ";
        inorderHelper(root);
        std::cout << std::endl;
    }

    void preorder() {
        std::cout << "Preorder:  ";
        preorderHelper(root);
        std::cout << std::endl;
    }

    void postorder() {
        std::cout << "Postorder: ";
        postorderHelper(root);
        std::cout << std::endl;
    }

    bool isValid() {
        if (root == TNULL) return true;
        if (root->color != BLACK) return false;
        int expectedBH = -1;
        return isRBUtil(root, 0, expectedBH);
    }

    int getBlackHeight() {
        Node<T>* node = root;
        int bh = 0;
        while (node != TNULL) {
            if (node->color == BLACK) bh++;
            node = node->left;
        }
        return bh;
    }

    void printTree() {
        if (root == TNULL) {
            std::cout << "Tree is empty." << std::endl;
            return;
        }
        std::cout << "Tree structure:" << std::endl;
        printTreeHelper(root, "", true);
    }

    bool isEmpty() { return root == TNULL; }
};

#endif
