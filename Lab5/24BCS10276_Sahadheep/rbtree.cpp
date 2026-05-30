#include <cstddef>

class RBTree {
public:
    RBTree() : root_(nullptr) {}

    ~RBTree() { clear(root_); }

    void insert(int key) {
        Node* z = new Node(key);
        Node* y = nullptr;
        Node* x = root_;

        while (x != nullptr) {
            y = x;
            if (z->key < x->key) {
                x = x->left;
            } else if (z->key > x->key) {
                x = x->right;
            } else {
                delete z;
                return;
            }
        }

        z->parent = y;
        if (y == nullptr) {
            root_ = z;
        } else if (z->key < y->key) {
            y->left = z;
        } else {
            y->right = z;
        }

        insertFixup(z);
    }

    bool contains(int key) const {
        Node* cur = root_;
        while (cur != nullptr) {
            if (key < cur->key) {
                cur = cur->left;
            } else if (key > cur->key) {
                cur = cur->right;
            } else {
                return true;
            }
        }
        return false;
    }

private:
    enum Color { Red, Black };

    struct Node {
        int key;
        Color color;
        Node* left;
        Node* right;
        Node* parent;

        explicit Node(int k)
            : key(k), color(Red), left(nullptr), right(nullptr), parent(nullptr) {}
    };

    Node* root_;

    void rotateLeft(Node* x) {
        Node* y = x->right;
        x->right = y->left;
        if (y->left != nullptr) {
            y->left->parent = x;
        }
        y->parent = x->parent;
        if (x->parent == nullptr) {
            root_ = y;
        } else if (x == x->parent->left) {
            x->parent->left = y;
        } else {
            x->parent->right = y;
        }
        y->left = x;
        x->parent = y;
    }

    void rotateRight(Node* y) {
        Node* x = y->left;
        y->left = x->right;
        if (x->right != nullptr) {
            x->right->parent = y;
        }
        x->parent = y->parent;
        if (y->parent == nullptr) {
            root_ = x;
        } else if (y == y->parent->right) {
            y->parent->right = x;
        } else {
            y->parent->left = x;
        }
        x->right = y;
        y->parent = x;
    }

    void insertFixup(Node* z) {
        while (z->parent != nullptr && z->parent->color == Red) {
            if (z->parent == z->parent->parent->left) {
                Node* y = z->parent->parent->right;
                if (y != nullptr && y->color == Red) {
                    z->parent->color = Black;
                    y->color = Black;
                    z->parent->parent->color = Red;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->right) {
                        z = z->parent;
                        rotateLeft(z);
                    }
                    z->parent->color = Black;
                    z->parent->parent->color = Red;
                    rotateRight(z->parent->parent);
                }
            } else {
                Node* y = z->parent->parent->left;
                if (y != nullptr && y->color == Red) {
                    z->parent->color = Black;
                    y->color = Black;
                    z->parent->parent->color = Red;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->left) {
                        z = z->parent;
                        rotateRight(z);
                    }
                    z->parent->color = Black;
                    z->parent->parent->color = Red;
                    rotateLeft(z->parent->parent);
                }
            }
        }
        root_->color = Black;
    }

    void clear(Node* node) {
        if (node == nullptr) {
            return;
        }
        clear(node->left);
        clear(node->right);
        delete node;
    }
};