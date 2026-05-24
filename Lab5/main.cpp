// Lab 5 - Red-Black Tree
// Siddhanth Kapoor (10154)
//
// self-balancing BST: every insert places the node by BST rules, paints it red,
// then fixes the red-black properties with recolouring + rotations so the tree
// stays roughly balanced (black-height equal on every root-to-leaf path).

#include <iostream>
#include <string>
#include <vector>

enum Color { RED, BLACK };

struct Node {
    int key;
    Color color;
    Node* left;
    Node* right;
    Node* parent;
};

class RedBlackTree {
public:
    RedBlackTree() {
        nil = new Node{0, BLACK, nullptr, nullptr, nullptr};
        root = nil;
    }

    ~RedBlackTree() {
        destroy(root);
        delete nil;
    }

    void insert(int key) {
        Node* z = new Node{key, RED, nil, nil, nil};
        Node* y = nil;
        Node* x = root;
        while (x != nil) {            // ordinary BST descent
            y = x;
            x = (key < x->key) ? x->left : x->right;
        }
        z->parent = y;
        if (y == nil) root = z;
        else if (key < y->key) y->left = z;
        else y->right = z;
        insertFixup(z);              // restore red-black properties
    }

    // returns true if key is present; prints the comparison path taken.
    bool search(int key) const {
        Node* x = root;
        std::cout << "search(" << key << "): ";
        while (x != nil) {
            std::cout << x->key << " ";
            if (key == x->key) { std::cout << "-> found\n"; return true; }
            x = (key < x->key) ? x->left : x->right;
        }
        std::cout << "-> not found\n";
        return false;
    }

    void inorder() const {
        std::cout << "inorder: ";
        inorder(root);
        std::cout << "\n";
    }

    // black-height check: returns the black height if valid, -1 if a property
    // is violated. also checks "no red node has a red child".
    bool verifyProperties() const {
        if (root->color != BLACK) { std::cout << "violation: root is not black\n"; return false; }
        bool redOk = true;
        int bh = blackHeight(root, redOk);
        if (!redOk) { std::cout << "violation: red node has a red child\n"; return false; }
        if (bh < 0) { std::cout << "violation: black height differs between paths\n"; return false; }
        std::cout << "properties hold (black-height = " << bh << ")\n";
        return true;
    }

private:
    Node* root;
    Node* nil; // single shared sentinel leaf, always black

    void leftRotate(Node* x) {
        Node* y = x->right;
        x->right = y->left;
        if (y->left != nil) y->left->parent = x;
        y->parent = x->parent;
        if (x->parent == nil) root = y;
        else if (x == x->parent->left) x->parent->left = y;
        else x->parent->right = y;
        y->left = x;
        x->parent = y;
    }

    void rightRotate(Node* x) {
        Node* y = x->left;
        x->left = y->right;
        if (y->right != nil) y->right->parent = x;
        y->parent = x->parent;
        if (x->parent == nil) root = y;
        else if (x == x->parent->right) x->parent->right = y;
        else x->parent->left = y;
        y->right = x;
        x->parent = y;
    }

    void insertFixup(Node* z) {
        while (z->parent->color == RED) {
            if (z->parent == z->parent->parent->left) {
                Node* uncle = z->parent->parent->right;
                if (uncle->color == RED) {            // case 1: recolour
                    z->parent->color = BLACK;
                    uncle->color = BLACK;
                    z->parent->parent->color = RED;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->right) {      // case 2: left rotate
                        z = z->parent;
                        leftRotate(z);
                    }
                    z->parent->color = BLACK;         // case 3: recolour + right rotate
                    z->parent->parent->color = RED;
                    rightRotate(z->parent->parent);
                }
            } else {                                  // mirror of the above
                Node* uncle = z->parent->parent->left;
                if (uncle->color == RED) {
                    z->parent->color = BLACK;
                    uncle->color = BLACK;
                    z->parent->parent->color = RED;
                    z = z->parent->parent;
                } else {
                    if (z == z->parent->left) {
                        z = z->parent;
                        rightRotate(z);
                    }
                    z->parent->color = BLACK;
                    z->parent->parent->color = RED;
                    leftRotate(z->parent->parent);
                }
            }
        }
        root->color = BLACK;
    }

    void inorder(Node* x) const {
        if (x == nil) return;
        inorder(x->left);
        std::cout << x->key << (x->color == RED ? "(R) " : "(B) ");
        inorder(x->right);
    }

    int blackHeight(Node* x, bool& redOk) const {
        if (x == nil) return 1; // nil counts as one black node
        if (x->color == RED && (x->left->color == RED || x->right->color == RED))
            redOk = false;
        int lh = blackHeight(x->left, redOk);
        int rh = blackHeight(x->right, redOk);
        if (lh != rh || lh < 0 || rh < 0) return -1;
        return lh + (x->color == BLACK ? 1 : 0);
    }

    void destroy(Node* x) {
        if (x == nil) return;
        destroy(x->left);
        destroy(x->right);
        delete x;
    }
};

int main() {
    RedBlackTree t;

    std::vector<int> values = {10, 20, 30, 15, 25, 5, 1, 40, 35, 50};
    std::cout << "== inserting: ";
    for (int v : values) std::cout << v << " ";
    std::cout << "==\n";
    for (int v : values) {
        t.insert(v);
        t.verifyProperties(); // properties hold after EVERY insertion
    }

    std::cout << "\n== inorder traversal (sorted, with colours) ==\n";
    t.inorder();

    std::cout << "\n== searches ==\n";
    t.search(25);  // present
    t.search(35);  // present
    t.search(99);  // absent

    std::cout << "\n== final property check ==\n";
    t.verifyProperties();
    return 0;
}
