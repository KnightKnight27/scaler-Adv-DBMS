#include <iostream>
#include <vector>
#include <algorithm>
#include <stdexcept>

using namespace std;

// ==========================================
// 1. Red-Black Tree Implementation
// ==========================================
enum Color { RED, BLACK };

struct RBTNode {
    int key;
    Color color;
    RBTNode *left, *right, *parent;

    RBTNode(int val) : key(val), color(RED), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RedBlackTree {
private:
    RBTNode* root;

    void leftRotate(RBTNode*& rootNode, RBTNode*& x) {
        RBTNode* y = x->right;
        x->right = y->left;

        if (y->left != nullptr) {
            y->left->parent = x;
        }

        y->parent = x->parent;

        if (x->parent == nullptr) {
            rootNode = y;
        } else if (x == x->parent->left) {
            x->parent->left = y;
        } else {
            x->parent->right = y;
        }

        y->left = x;
        x->parent = y;
    }

    void rightRotate(RBTNode*& rootNode, RBTNode*& x) {
        RBTNode* y = x->left;
        x->left = y->right;

        if (y->right != nullptr) {
            y->right->parent = x;
        }

        y->parent = x->parent;

        if (x->parent == nullptr) {
            rootNode = y;
        } else if (x == x->parent->left) {
            x->parent->left = y;
        } else {
            x->parent->right = y;
        }

        y->right = x;
        x->parent = y;
    }

    void balanceInsert(RBTNode*& rootNode, RBTNode*& z) {
        while (z->parent != nullptr && z->parent->color == RED) {
            RBTNode* parentNode = z->parent;
            RBTNode* grandparent = parentNode->parent;

            if (parentNode == grandparent->left) {
                RBTNode* uncle = grandparent->right;

                if (uncle != nullptr && uncle->color == RED) {
                    parentNode->color = BLACK;
                    uncle->color = BLACK;
                    grandparent->color = RED;
                    z = grandparent;
                } else {
                    if (z == parentNode->right) {
                        leftRotate(rootNode, parentNode);
                        z = parentNode;
                        parentNode = z->parent;
                    }
                    rightRotate(rootNode, grandparent);
                    swap(parentNode->color, grandparent->color);
                    z = parentNode;
                }
            } else {
                RBTNode* uncle = grandparent->left;

                if (uncle != nullptr && uncle->color == RED) {
                    parentNode->color = BLACK;
                    uncle->color = BLACK;
                    grandparent->color = RED;
                    z = grandparent;
                } else {
                    if (z == parentNode->left) {
                        rightRotate(rootNode, parentNode);
                        z = parentNode;
                        parentNode = z->parent;
                    }
                    leftRotate(rootNode, grandparent);
                    swap(parentNode->color, grandparent->color);
                    z = parentNode;
                }
            }
        }
        rootNode->color = BLACK;
    }

    void inOrderHelper(RBTNode* node) const {
        if (node == nullptr) return;
        inOrderHelper(node->left);
        cout << node->key << "(" << (node->color == RED ? "R" : "B") << ") ";
        inOrderHelper(node->right);
    }

    void destroyTree(RBTNode* node) {
        if (node != nullptr) {
            destroyTree(node->left);
            destroyTree(node->right);
            delete node;
        }
    }

public:
    RedBlackTree() : root(nullptr) {}
    ~RedBlackTree() { destroyTree(root); }

    void insert(int key) {
        RBTNode* z = new RBTNode(key);
        RBTNode* parent = nullptr;
        RBTNode* current = root;

        while (current != nullptr) {
            parent = current;
            if (z->key < current->key) {
                current = current->left;
            } else {
                current = current->right;
            }
        }

        z->parent = parent;
        if (parent == nullptr) {
            root = z;
        } else if (z->key < parent->key) {
            parent->left = z;
        } else {
            parent->right = z;
        }

        balanceInsert(root, z);
    }

    bool contains(int key) const {
        RBTNode* current = root;
        while (current != nullptr) {
            if (key == current->key) return true;
            else if (key < current->key) current = current->left;
            else current = current->right;
        }
        return false;
    }

    void print() const {
        inOrderHelper(root);
        cout << "\n";
    }
};

// ==========================================
// 2. B-Tree Implementation (CLRS-Compliant)
// ==========================================
class BTreeNode {
public:
    int *keys;     // Array of keys
    int t;         // Minimum degree
    BTreeNode **C; // Array of child pointers
    int n;         // Current number of keys
    bool leaf;     // True when node is leaf

    BTreeNode(int _t, bool _leaf) {
        t = _t;
        leaf = _leaf;
        keys = new int[2 * t - 1];
        C = new BTreeNode*[2 * t];
        n = 0;
        for (int i = 0; i < 2 * t; i++) {
            C[i] = nullptr;
        }
    }

    ~BTreeNode() {
        delete[] keys;
        delete[] C;
    }

    int findKey(int k) {
        int idx = 0;
        while (idx < n && keys[idx] < k) {
            idx++;
        }
        return idx;
    }

    void traverse(int indent = 0) {
        int i;
        for (i = 0; i < n; i++) {
            if (!leaf && C[i]) {
                C[i]->traverse(indent + 4);
            }
            for (int space = 0; space < indent; space++) cout << " ";
            cout << keys[i] << "\n";
        }
        if (!leaf && C[i]) {
            C[i]->traverse(indent + 4);
        }
    }

    BTreeNode *search(int k) {
        int i = 0;
        while (i < n && k > keys[i]) {
            i++;
        }
        if (i < n && keys[i] == k) return this;
        if (leaf) return nullptr;
        return C[i]->search(k);
    }

    void insertNonFull(int k) {
        int i = n - 1;
        if (leaf) {
            while (i >= 0 && keys[i] > k) {
                keys[i + 1] = keys[i];
                i--;
            }
            keys[i + 1] = k;
            n = n + 1;
        } else {
            while (i >= 0 && keys[i] > k) {
                i--;
            }
            if (C[i + 1]->n == 2 * t - 1) {
                splitChild(i + 1, C[i + 1]);
                if (keys[i + 1] < k) {
                    i++;
                }
            }
            C[i + 1]->insertNonFull(k);
        }
    }

    void splitChild(int i, BTreeNode *y) {
        BTreeNode *z = new BTreeNode(y->t, y->leaf);
        z->n = t - 1;

        for (int j = 0; j < t - 1; j++) {
            z->keys[j] = y->keys[j + t];
        }

        if (!y->leaf) {
            for (int j = 0; j < t; j++) {
                z->C[j] = y->C[j + t];
            }
        }

        y->n = t - 1;

        for (int j = n; j >= i + 1; j--) {
            C[j + 1] = C[j];
        }
        C[i + 1] = z;

        for (int j = n - 1; j >= i; j--) {
            keys[j + 1] = keys[j];
        }
        keys[i] = y->keys[t - 1];
        n = n + 1;
    }

    void remove(int k) {
        int idx = findKey(k);
        if (idx < n && keys[idx] == k) {
            if (leaf) {
                removeFromLeaf(idx);
            } else {
                removeFromNonLeaf(idx);
            }
        } else {
            if (leaf) {
                cout << "Key " << k << " does not exist in B-Tree.\n";
                return;
            }
            bool flag = ((idx == n) ? true : false);
            if (C[idx]->n < t) {
                fill(idx);
            }
            if (flag && idx > n) {
                C[idx - 1]->remove(k);
            } else {
                C[idx]->remove(k);
            }
        }
    }

    void removeFromLeaf(int idx) {
        for (int i = idx + 1; i < n; ++i) {
            keys[i - 1] = keys[i];
        }
        n--;
    }

    void removeFromNonLeaf(int idx) {
        int k = keys[idx];
        if (C[idx]->n >= t) {
            int pred = getPred(idx);
            keys[idx] = pred;
            C[idx]->remove(pred);
        } else if (C[idx + 1]->n >= t) {
            int succ = getSucc(idx);
            keys[idx] = succ;
            C[idx + 1]->remove(succ);
        } else {
            merge(idx);
            C[idx]->remove(k);
        }
    }

    int getPred(int idx) {
        BTreeNode *cur = C[idx];
        while (!cur->leaf) {
            cur = cur->C[cur->n];
        }
        return cur->keys[cur->n - 1];
    }

    int getSucc(int idx) {
        BTreeNode *cur = C[idx + 1];
        while (!cur->leaf) {
            cur = cur->C[0];
        }
        return cur->keys[0];
    }

    void fill(int idx) {
        if (idx != 0 && C[idx - 1]->n >= t) {
            borrowFromPrev(idx);
        } else if (idx != n && C[idx + 1]->n >= t) {
            borrowFromNext(idx);
        } else {
            if (idx != n) {
                merge(idx);
            } else {
                merge(idx - 1);
            }
        }
    }

    void borrowFromPrev(int idx) {
        BTreeNode *child = C[idx];
        BTreeNode *sibling = C[idx - 1];

        for (int i = child->n - 1; i >= 0; --i) {
            child->keys[i + 1] = child->keys[i];
        }

        if (!child->leaf) {
            for (int i = child->n; i >= 0; --i) {
                child->C[i + 1] = child->C[i];
            }
        }

        child->keys[0] = keys[idx - 1];
        if (!child->leaf) {
            child->C[0] = sibling->C[sibling->n];
        }

        keys[idx - 1] = sibling->keys[sibling->n - 1];

        child->n += 1;
        sibling->n -= 1;
    }

    void borrowFromNext(int idx) {
        BTreeNode *child = C[idx];
        BTreeNode *sibling = C[idx + 1];

        child->keys[child->n] = keys[idx];

        if (!child->leaf) {
            child->C[child->n + 1] = sibling->C[0];
        }

        keys[idx] = sibling->keys[0];

        for (int i = 1; i < sibling->n; ++i) {
            sibling->keys[i - 1] = sibling->keys[i];
        }

        if (!sibling->leaf) {
            for (int i = 1; i <= sibling->n; ++i) {
                sibling->C[i - 1] = sibling->C[i];
            }
        }

        child->n += 1;
        sibling->n -= 1;
    }

    void merge(int idx) {
        BTreeNode *child = C[idx];
        BTreeNode *sibling = C[idx + 1];

        child->keys[t - 1] = keys[idx];

        for (int i = 0; i < sibling->n; ++i) {
            child->keys[i + t] = sibling->keys[i];
        }

        if (!child->leaf) {
            for (int i = 0; i <= sibling->n; ++i) {
                child->C[i + t] = sibling->C[i];
            }
        }

        for (int i = idx + 1; i < n; ++i) {
            keys[i - 1] = keys[i];
        }

        for (int i = idx + 2; i <= n; ++i) {
            C[i - 1] = C[i];
        }

        child->n += sibling->n + 1;
        n--;

        delete sibling;
    }
};

class BTree {
private:
    BTreeNode *root;
    int t;

    void deleteTree(BTreeNode *node) {
        if (node != nullptr) {
            if (!node->leaf) {
                for (int i = 0; i <= node->n; i++) {
                    deleteTree(node->C[i]);
                }
            }
            delete node;
        }
    }

public:
    BTree(int _t) {
        root = nullptr;
        t = _t;
    }

    ~BTree() {
        deleteTree(root);
    }

    void traverse() {
        if (root != nullptr) root->traverse();
    }

    BTreeNode *search(int k) {
        return (root == nullptr) ? nullptr : root->search(k);
    }

    void insert(int k) {
        if (root == nullptr) {
            root = new BTreeNode(t, true);
            root->keys[0] = k;
            root->n = 1;
        } else {
            if (root->n == 2 * t - 1) {
                BTreeNode *s = new BTreeNode(t, false);
                s->C[0] = root;
                s->splitChild(0, root);
                int i = 0;
                if (s->keys[0] < k) {
                    i++;
                }
                s->C[i]->insertNonFull(k);
                root = s;
            } else {
                root->insertNonFull(k);
            }
        }
    }

    void remove(int k) {
        if (!root) {
            cout << "The B-Tree is empty.\n";
            return;
        }
        root->remove(k);
        if (root->n == 0) {
            BTreeNode *tmp = root;
            if (root->leaf) {
                root = nullptr;
            } else {
                root = root->C[0];
            }
            delete tmp;
        }
    }
};

// ==========================================
// 3. Test Runner
// ==========================================
int main() {
    // --- Red-Black Tree Tests ---
    cout << "========================================\n";
    cout << "RUNNING RED-BLACK TREE TESTS\n";
    cout << "========================================\n";
    RedBlackTree rbt;
    rbt.insert(45);
    rbt.insert(26);
    rbt.insert(72);
    rbt.insert(18);
    rbt.insert(35);
    rbt.insert(10);

    cout << "RB-Tree In-order (sorted keys with colors R/B):\n";
    rbt.print();
    cout << "Contains 18? " << (rbt.contains(18) ? "Yes" : "No") << "\n";
    cout << "Contains 99? " << (rbt.contains(99) ? "Yes" : "No") << "\n";

    // --- B-Tree Tests ---
    cout << "\n========================================\n";
    cout << "RUNNING B-TREE TESTS (Degree t = 3)\n";
    cout << "========================================\n";
    BTree t(3); // A B-Tree with minimum degree 3

    // Insert keys
    int keysToInsert[] = {1, 3, 7, 10, 11, 13, 14, 15, 18, 16, 19, 24, 25, 26, 21, 4, 5, 20, 22, 2, 17, 12, 6};
    int numKeys = sizeof(keysToInsert) / sizeof(keysToInsert[0]);
    cout << "Inserting keys: ";
    for (int i = 0; i < numKeys; i++) {
        cout << keysToInsert[i] << " ";
        t.insert(keysToInsert[i]);
    }
    cout << "\n\nB-Tree structure (indented keys representation):\n";
    t.traverse();

    // Search keys
    int searchKey1 = 18;
    int searchKey2 = 99;
    cout << "\nSearch key " << searchKey1 << ": " << (t.search(searchKey1) ? "FOUND" : "NOT FOUND") << "\n";
    cout << "Search key " << searchKey2 << ": " << (t.search(searchKey2) ? "FOUND" : "NOT FOUND") << "\n";

    // Delete keys
    int keysToDelete[] = {6, 13, 7, 4, 2};
    int numDelete = sizeof(keysToDelete) / sizeof(keysToDelete[0]);
    for (int i = 0; i < numDelete; i++) {
        cout << "\nRemoving key " << keysToDelete[i] << "...\n";
        t.remove(keysToDelete[i]);
        cout << "B-Tree structure after removal:\n";
        t.traverse();
    }

    return 0;
}
