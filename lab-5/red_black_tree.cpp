/*
 * Red-Black Tree — Lab 5
 *
 * Rules:
 *  1. Every node is RED or BLACK.
 *  2. Root is always BLACK.
 *  3. Null pointers count as BLACK.
 *  4. A RED node must have two BLACK children.
 *  5. Every root-to-null path has the same number of BLACK nodes.
 */

#include <vector>
#include <limits>

#pragma GCC diagnostic ignored "-Wcomment"

enum Color { RED, BLACK };

struct Node {
    int   key;
    Color color;
    Node* parent;
    Node* left;
    Node* right;

    Node(int value) : key(value), color(RED), parent(nullptr), left(nullptr), right(nullptr) {}
};

// Treat null as BLACK (covers rule 3 everywhere)
Color colorOf(Node* node) {
    return node == nullptr ? BLACK : node->color;
}

class RedBlackTree {
public:
    RedBlackTree() : root(nullptr) {}
    ~RedBlackTree() { deleteAll(root); }

    void insert(int key);
    bool search(int key);
    bool isValid();

private:
    Node* root;

    void rotateLeft(Node* x);
    void rotateRight(Node* y);
    void fixAfterInsert(Node* node);
    bool validateHelper(Node* node, int lo, int hi, int blacks, int& expected);
    void deleteAll(Node* node) {
        if (!node) return;
        deleteAll(node->left);
        deleteAll(node->right);
        delete node;
    }
};

// ── Insert ────────────────────────────────────────────────────────
void RedBlackTree::insert(int key) {
    Node* parent  = nullptr;
    Node* current = root;

    while (current != nullptr) {
        parent = current;
        if      (key == current->key) return;   // duplicate — skip
        else if (key <  current->key) current = current->left;
        else                          current = current->right;
    }

    Node* newNode   = new Node(key);
    newNode->parent = parent;

    if      (parent == nullptr)    root          = newNode;
    else if (key < parent->key)    parent->left  = newNode;
    else                           parent->right = newNode;

    fixAfterInsert(newNode);
}

// ── Left rotation ─────────────────────────────────────────────────
//   X              Y
//  / \    ->      / \
// A   Y          X   C
//    / \        / \
//   B   C      A   B
void RedBlackTree::rotateLeft(Node* X) {
    Node* Y  = X->right;
    X->right = Y->left;
    if (Y->left) Y->left->parent = X;

    Y->parent = X->parent;
    if      (!X->parent)           root             = Y;
    else if (X == X->parent->left) X->parent->left  = Y;
    else                           X->parent->right = Y;

    Y->left   = X;
    X->parent = Y;
}

// ── Right rotation ────────────────────────────────────────────────
//     Y          X
//    / \  ->    / \
//   X   C      A   Y
//  / \             / \
// A   B           B   C
void RedBlackTree::rotateRight(Node* Y) {
    Node* X = Y->left;
    Y->left = X->right;
    if (X->right) X->right->parent = Y;

    X->parent = Y->parent;
    if      (!Y->parent)            root             = X;
    else if (Y == Y->parent->right) Y->parent->right = X;
    else                            Y->parent->left  = X;

    X->right  = Y;
    Y->parent = X;
}

// ── Fix violations after insert ───────────────────────────────────
// Case 1 — uncle RED:  recolour parent, uncle, grandparent; move up.
// Case 2 — uncle BLACK, inner child: rotate parent -> becomes case 3.
// Case 3 — uncle BLACK, outer child: recolour + rotate grandparent.
void RedBlackTree::fixAfterInsert(Node* node) {
    while (node != root && colorOf(node->parent) == RED) {
        Node* parent      = node->parent;
        Node* grandparent = parent->parent;

        if (parent == grandparent->left) {
            Node* uncle = grandparent->right;

            if (colorOf(uncle) == RED) {                    // Case 1
                parent->color      = BLACK;
                uncle->color       = BLACK;
                grandparent->color = RED;
                node = grandparent;
            } else {
                if (node == parent->right) {                // Case 2
                    node = parent;
                    rotateLeft(node);
                    parent      = node->parent;
                    grandparent = parent->parent;
                }
                parent->color      = BLACK;                 // Case 3
                grandparent->color = RED;
                rotateRight(grandparent);
            }
        } else {                                            // mirror
            Node* uncle = grandparent->left;

            if (colorOf(uncle) == RED) {                    // Case 1
                parent->color      = BLACK;
                uncle->color       = BLACK;
                grandparent->color = RED;
                node = grandparent;
            } else {
                if (node == parent->left) {                 // Case 2
                    node = parent;
                    rotateRight(node);
                    parent      = node->parent;
                    grandparent = parent->parent;
                }
                parent->color      = BLACK;                 // Case 3
                grandparent->color = RED;
                rotateLeft(grandparent);
            }
        }
    }
    root->color = BLACK;   // rule 2
}

// ── Search ────────────────────────────────────────────────────────
bool RedBlackTree::search(int key) {
    Node* current = root;
    while (current != nullptr) {
        if      (key == current->key) return true;
        else if (key <  current->key) current = current->left;
        else                          current = current->right;
    }
    return false;
}

// ── Validation ────────────────────────────────────────────────────
bool RedBlackTree::validateHelper(Node* node, int lo, int hi,
                                  int blacks, int& expected) {
    if (node == nullptr) {
        if (expected == -1) { expected = blacks; return true; }
        return blacks == expected;   // rule 5
    }

    if (node->key <= lo || node->key >= hi) return false;   // BST order

    if (node->color == BLACK) blacks++;

    if (node->color == RED &&                               // rule 4
        (colorOf(node->left) == RED || colorOf(node->right) == RED))
        return false;

    return validateHelper(node->left,  lo,       node->key, blacks, expected)
        && validateHelper(node->right, node->key, hi,       blacks, expected);
}

bool RedBlackTree::isValid() {
    if (root && root->color != BLACK) return false;   // rule 2
    int expected = -1;
    return validateHelper(root,
                          std::numeric_limits<int>::min(),
                          std::numeric_limits<int>::max(),
                          0, expected);
}

int main() {
    RedBlackTree tree;
    std::vector<int> values = { 41, 38, 31, 12, 19, 8, 25, 50, 60, 55, 5, 1, 70 };

    for (int v : values)
        tree.insert(v);

    tree.search(25);
    tree.search(99);
    tree.isValid();

    return 0;
}
