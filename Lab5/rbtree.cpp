#include <iostream>
#include <vector>
#include <limits>

using namespace std;

enum Color { RED, BLACK };

struct Node {
    int key;
    Color color;
    Node* parent;
    Node* left;
    Node* right;

    Node(int value) {
        key = value;
        color = RED;
        parent = left = right = nullptr;
    }
};

Color colorOf(Node* node) {
    return node == nullptr ? BLACK : node->color;
}

class RedBlackTree {
public:
    RedBlackTree() : root(nullptr) {}

    RedBlackTree(const RedBlackTree&) = delete;
    RedBlackTree& operator=(const RedBlackTree&) = delete;

    ~RedBlackTree() {
        deleteAll(root);
    }

    void insert(int key);
    bool search(int key);
    bool isValid();

private:
    Node* root;

    void rotateLeft(Node* x);
    void rotateRight(Node* y);
    void fixAfterInsert(Node* node);
    bool validateHelper(Node* node, int lo, int hi, int blacks, int& expected);
    void deleteAll(Node* node);
};

void RedBlackTree::deleteAll(Node* node) {
    if (!node) return;
    deleteAll(node->left);
    deleteAll(node->right);
    delete node;
}

void RedBlackTree::insert(int key) {
    Node* parent = nullptr;
    Node* current = root;

    while (current != nullptr) {
        parent = current;

        if (key == current->key)
            return;
        else if (key < current->key)
            current = current->left;
        else
            current = current->right;
    }

    Node* newNode = new Node(key);
    newNode->parent = parent;

    if (parent == nullptr)
        root = newNode;
    else if (key < parent->key)
        parent->left = newNode;
    else
        parent->right = newNode;

    fixAfterInsert(newNode);
}

void RedBlackTree::rotateLeft(Node* x) {
    Node* y = x->right;
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

void RedBlackTree::rotateRight(Node* y) {
    Node* x = y->left;
    y->left = x->right;

    if (x->right)
        x->right->parent = y;

    x->parent = y->parent;

    if (!y->parent)
        root = x;
    else if (y == y->parent->right)
        y->parent->right = x;
    else
        y->parent->left = x;

    x->right = y;
    y->parent = x;
}

void RedBlackTree::fixAfterInsert(Node* node) {
    while (node != root && colorOf(node->parent) == RED) {
        Node* parent = node->parent;
        Node* grandparent = parent->parent;

        if (parent == grandparent->left) {
            Node* uncle = grandparent->right;

            if (colorOf(uncle) == RED) {
                parent->color = BLACK;
                uncle->color = BLACK;
                grandparent->color = RED;
                node = grandparent;
            } else {
                if (node == parent->right) {
                    node = parent;
                    rotateLeft(node);
                    parent = node->parent;
                    grandparent = parent->parent;
                }

                parent->color = BLACK;
                grandparent->color = RED;
                rotateRight(grandparent);
            }
        } else {
            Node* uncle = grandparent->left;

            if (colorOf(uncle) == RED) {
                parent->color = BLACK;
                uncle->color = BLACK;
                grandparent->color = RED;
                node = grandparent;
            } else {
                if (node == parent->left) {
                    node = parent;
                    rotateRight(node);
                    parent = node->parent;
                    grandparent = parent->parent;
                }

                parent->color = BLACK;
                grandparent->color = RED;
                rotateLeft(grandparent);
            }
        }
    }

    root->color = BLACK;
}

bool RedBlackTree::search(int key) {
    Node* current = root;

    while (current != nullptr) {
        if (key == current->key)
            return true;
        else if (key < current->key)
            current = current->left;
        else
            current = current->right;
    }

    return false;
}

bool RedBlackTree::validateHelper(Node* node, int lo, int hi, int blacks, int& expected) {
    if (node == nullptr) {
        if (expected == -1) {
            expected = blacks;
            return true;
        }
        return blacks == expected;
    }

    if (node->key <= lo || node->key >= hi)
        return false;

    if (node->color == BLACK)
        blacks++;

    if (node->color == RED &&
        (colorOf(node->left) == RED || colorOf(node->right) == RED))
        return false;

    return validateHelper(node->left, lo, node->key, blacks, expected) &&
           validateHelper(node->right, node->key, hi, blacks, expected);
}

bool RedBlackTree::isValid() {
    if (root && root->color != BLACK)
        return false;

    int expected = -1;

    return validateHelper(
        root,
        numeric_limits<int>::min(),
        numeric_limits<int>::max(),
        0,
        expected
    );
}

int main() {
    RedBlackTree tree;

    vector<int> values = {41, 38, 31, 12, 19, 8, 25, 50, 60, 55};

    for (int v : values)
        tree.insert(v);

    cout << "Search 25: " << tree.search(25) << endl;
    cout << "Search 99: " << tree.search(99) << endl;
    cout << "Tree Valid: " << tree.isValid() << endl;

    return 0;
}