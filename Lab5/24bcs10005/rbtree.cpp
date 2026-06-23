#include <cstddef>

class RedBlackSet {
public:
    RedBlackSet() : root(nullptr), count(0) {}

    ~RedBlackSet() { destroy(root); }

    // Insert a key; returns true if inserted, false if already present
    bool add(int key) {
        Node* node = new Node(key);
        Node* parent = nullptr;
        Node* cur = root;

        while (cur != nullptr) {
            parent = cur;
            if (key < cur->key) cur = cur->left;
            else if (key > cur->key) cur = cur->right;
            else { delete node; return false; }
        }

        node->parent = parent;
        if (parent == nullptr) root = node;
        else if (key < parent->key) parent->left = node;
        else parent->right = node;

        balanceAfterInsert(node);
        ++count;
        return true;
    }

    bool contains(int key) const {
        Node* cur = root;
        while (cur) {
            if (key < cur->key) cur = cur->left;
            else if (key > cur->key) cur = cur->right;
            else return true;
        }
        return false;
    }

    std::size_t size() const { return count; }

private:
    enum Color { RED, BLACK };

    struct Node {
        int key;
        Node* left;
        Node* right;
        Node* parent;
        Color color;
        explicit Node(int k) : key(k), left(nullptr), right(nullptr), parent(nullptr), color(RED) {}
    };

    Node* root;
    std::size_t count;

    void rotateLeft(Node* x) {
        Node* y = x->right;
        if (!y) return;
        x->right = y->left;
        if (y->left) y->left->parent = x;
        y->parent = x->parent;
        if (!x->parent) root = y;
        else if (x == x->parent->left) x->parent->left = y;
        else x->parent->right = y;
        y->left = x;
        x->parent = y;
    }

    void rotateRight(Node* x) {
        Node* y = x->left;
        if (!y) return;
        x->left = y->right;
        if (y->right) y->right->parent = x;
        y->parent = x->parent;
        if (!x->parent) root = y;
        else if (x == x->parent->right) x->parent->right = y;
        else x->parent->left = y;
        y->right = x;
        x->parent = y;
    }

    void balanceAfterInsert(Node* z) {
        while (z->parent && z->parent->color == RED) {
            Node* gp = z->parent->parent;
            if (!gp) break;
            if (z->parent == gp->left) {
                Node* uncle = gp->right;
                if (uncle && uncle->color == RED) {
                    z->parent->color = BLACK;
                    uncle->color = BLACK;
                    gp->color = RED;
                    z = gp;
                } else {
                    if (z == z->parent->right) {
                        z = z->parent;
                        rotateLeft(z);
                    }
                    z->parent->color = BLACK;
                    gp->color = RED;
                    rotateRight(gp);
                }
            } else {
                Node* uncle = gp->left;
                if (uncle && uncle->color == RED) {
                    z->parent->color = BLACK;
                    uncle->color = BLACK;
                    gp->color = RED;
                    z = gp;
                } else {
                    if (z == z->parent->left) {
                        z = z->parent;
                        rotateRight(z);
                    }
                    z->parent->color = BLACK;
                    gp->color = RED;
                    rotateLeft(gp);
                }
            }
        }
        if (root) root->color = BLACK;
    }

    void destroy(Node* n) {
        if (!n) return;
        destroy(n->left);
        destroy(n->right);
        delete n;
    }
};
