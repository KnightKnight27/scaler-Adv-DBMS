#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>
#include <cassert>
#include <string>
#include <iomanip>
#include <sstream>

// ============================================================================
// PART 1: RED-BLACK TREE IMPLEMENTATION
// ============================================================================

enum Color { RED, BLACK };

struct RBTNode {
    int key;
    Color color;
    RBTNode* left;
    RBTNode* right;
    RBTNode* parent;

    RBTNode(int k, Color c = RED, RBTNode* l = nullptr, RBTNode* r = nullptr, RBTNode* p = nullptr)
        : key(k), color(c), left(l), right(r), parent(p) {}
};

class RedBlackTree {
private:
    RBTNode* root;
    RBTNode* TNULL; // Sentinel node representing null pointers

    void initializeNULLNode(RBTNode* node, RBTNode* parent) {
        node->key = 0;
        node->color = BLACK;
        node->left = nullptr;
        node->right = nullptr;
        node->parent = parent;
    }

    void leftRotate(RBTNode* x) {
        RBTNode* y = x->right;
        x->right = y->left;
        if (y->left != TNULL) {
            y->left->parent = x;
        }
        y->parent = x->parent;
        if (x->parent == nullptr) {
            this->root = y;
        } else if (x == x->parent->left) {
            x->parent->left = y;
        } else {
            x->parent->right = y;
        }
        y->left = x;
        x->parent = y;
    }

    void rightRotate(RBTNode* x) {
        RBTNode* y = x->left;
        x->left = y->right;
        if (y->right != TNULL) {
            y->right->parent = x;
        }
        y->parent = x->parent;
        if (x->parent == nullptr) {
            this->root = y;
        } else if (x == x->parent->right) {
            x->parent->right = y;
        } else {
            x->parent->left = y;
        }
        y->right = x;
        x->parent = y;
    }

    void insertFix(RBTNode* k) {
        RBTNode* u;
        while (k->parent->color == RED) {
            if (k->parent == k->parent->parent->right) {
                u = k->parent->parent->left; // uncle
                if (u->color == RED) {
                    // Case 1: Uncle is Red -> Recoloring
                    u->color = BLACK;
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    k = k->parent->parent;
                } else {
                    if (k == k->parent->left) {
                        // Case 2: Uncle is Black, Node is Left Child -> Right Rotate parent
                        k = k->parent;
                        rightRotate(k);
                    }
                    // Case 3: Uncle is Black, Node is Right Child -> Left Rotate grandparent
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    leftRotate(k->parent->parent);
                }
            } else {
                u = k->parent->parent->right; // uncle

                if (u->color == RED) {
                    // Case 1 symmetric
                    u->color = BLACK;
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    k = k->parent->parent;
                } else {
                    if (k == k->parent->right) {
                        // Case 2 symmetric
                        k = k->parent;
                        leftRotate(k);
                    }
                    // Case 3 symmetric
                    k->parent->color = BLACK;
                    k->parent->parent->color = RED;
                    rightRotate(k->parent->parent);
                }
            }
            if (k == root) {
                break;
            }
        }
        root->color = BLACK;
    }

    void rbTransplant(RBTNode* u, RBTNode* v) {
        if (u->parent == nullptr) {
            root = v;
        } else if (u == u->parent->left) {
            u->parent->left = v;
        } else {
            u->parent->right = v;
        }
        v->parent = u->parent;
    }

    void deleteFix(RBTNode* x) {
        RBTNode* s;
        while (x != root && x->color == BLACK) {
            if (x == x->parent->left) {
                s = x->parent->right;
                if (s->color == RED) {
                    // Case 1: Sibling is Red
                    s->color = BLACK;
                    x->parent->color = RED;
                    leftRotate(x->parent);
                    s = x->parent->right;
                }

                if (s->left->color == BLACK && s->right->color == BLACK) {
                    // Case 2: Sibling and its children are Black
                    s->color = RED;
                    x = x->parent;
                } else {
                    if (s->right->color == BLACK) {
                        // Case 3: Sibling is Black, sibling's left child is Red
                        s->left->color = BLACK;
                        s->color = RED;
                        rightRotate(s);
                        s = x->parent->right;
                    }

                    // Case 4: Sibling is Black, sibling's right child is Red
                    s->color = x->parent->color;
                    x->parent->color = BLACK;
                    s->right->color = BLACK;
                    leftRotate(x->parent);
                    x = root;
                }
            } else {
                s = x->parent->left;
                if (s->color == RED) {
                    // Case 1 symmetric
                    s->color = BLACK;
                    x->parent->color = RED;
                    rightRotate(x->parent);
                    s = x->parent->left;
                }

                if (s->left->color == BLACK && s->right->color == BLACK) {
                    // Case 2 symmetric
                    s->color = RED;
                    x = x->parent;
                } else {
                    if (s->left->color == BLACK) {
                        // Case 3 symmetric
                        s->right->color = BLACK;
                        s->color = RED;
                        leftRotate(s);
                        s = x->parent->left;
                    }

                    // Case 4 symmetric
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

    RBTNode* minimum(RBTNode* node) {
        while (node->left != TNULL) {
            node = node->left;
        }
        return node;
    }

    void printHelper(RBTNode* root, std::string indent, bool last) {
        if (root != TNULL) {
            std::cout << indent;
            if (last) {
                std::cout << "R----";
                indent += "   ";
            } else {
                std::cout << "L----";
                indent += "|  ";
            }

            std::string sColor = (root->color == RED) ? "\033[31mRED\033[0m" : "\033[90mBLACK\033[0m";
            std::cout << root->key << " (" << sColor << ")" << std::endl;
            printHelper(root->left, indent, false);
            printHelper(root->right, indent, true);
        }
    }

    // Invariants checking
    int checkBlackHeight(RBTNode* node, bool& valid) {
        if (node == TNULL) return 1;
        
        if (node->color == RED) {
            if (node->left->color == RED || node->right->color == RED) {
                std::cerr << "Violation: Consecutive RED nodes found! Parent: " << node->key << std::endl;
                valid = false;
            }
        }

        int leftHeight = checkBlackHeight(node->left, valid);
        int rightHeight = checkBlackHeight(node->right, valid);

        if (leftHeight != rightHeight) {
            std::cerr << "Violation: Black height mismatch at node " << node->key 
                      << " (Left: " << leftHeight << ", Right: " << rightHeight << ")" << std::endl;
            valid = false;
        }

        return leftHeight + (node->color == BLACK ? 1 : 0);
    }

public:
    RedBlackTree() {
        TNULL = new RBTNode(0);
        TNULL->color = BLACK;
        TNULL->left = nullptr;
        TNULL->right = nullptr;
        root = TNULL;
    }

    ~RedBlackTree() {
        // Simple cleanup
        std::queue<RBTNode*> q;
        if (root != TNULL) q.push(root);
        while (!q.empty()) {
            RBTNode* curr = q.front();
            q.pop();
            if (curr->left != TNULL) q.push(curr->left);
            if (curr->right != TNULL) q.push(curr->right);
            delete curr;
        }
        delete TNULL;
    }

    RBTNode* search(int k) {
        RBTNode* curr = root;
        while (curr != TNULL && k != curr->key) {
            if (k < curr->key) curr = curr->left;
            else curr = curr->right;
        }
        return (curr == TNULL) ? nullptr : curr;
    }

    void insert(int key) {
        RBTNode* node = new RBTNode(key);
        node->parent = nullptr;
        node->key = key;
        node->left = TNULL;
        node->right = TNULL;
        node->color = RED;

        RBTNode* y = nullptr;
        RBTNode* x = this->root;

        while (x != TNULL) {
            y = x;
            if (node->key < x->key) {
                x = x->left;
            } else {
                x = x->right;
            }
        }

        node->parent = y;
        if (y == nullptr) {
            root = node;
        } else if (node->key < y->key) {
            y->left = node;
        } else {
            y->right = node;
        }

        if (node->parent == nullptr) {
            node->color = BLACK;
            return;
        }

        if (node->parent->parent == nullptr) {
            return;
        }

        insertFix(node);
    }

    void remove(int key) {
        RBTNode* z = TNULL;
        RBTNode* x, * y;
        RBTNode* curr = root;
        while (curr != TNULL) {
            if (curr->key == key) {
                z = curr;
                break;
            }
            if (curr->key <= key) {
                curr = curr->right;
            } else {
                curr = curr->left;
            }
        }

        if (z == TNULL) {
            std::cout << "Key " << key << " not found in RBT." << std::endl;
            return;
        }

        y = z;
        Color y_original_color = y->color;
        if (z->left == TNULL) {
            x = z->right;
            rbTransplant(z, z->right);
        } else if (z->right == TNULL) {
            x = z->left;
            rbTransplant(z, z->left);
        } else {
            y = minimum(z->right);
            y_original_color = y->color;
            x = y->right;
            if (y->parent == z) {
                x->parent = y;
            } else {
                rbTransplant(y, y->right);
                y->right = z->right;
                y->right->parent = y;
            }

            rbTransplant(z, y);
            y->left = z->left;
            y->left->parent = y;
            y->color = z->color;
        }
        delete z;
        if (y_original_color == BLACK) {
            deleteFix(x);
        }
    }

    bool verifyInvariants() {
        if (root == TNULL) return true;
        if (root->color != BLACK) {
            std::cerr << "Violation: Root is not BLACK!" << std::endl;
            return false;
        }
        bool valid = true;
        checkBlackHeight(root, valid);
        return valid;
    }

    void printTree() {
        if (root == TNULL) {
            std::cout << "Empty Tree" << std::endl;
            return;
        }
        printHelper(this->root, "", true);
    }
};

// ============================================================================
// PART 2: B-TREE IMPLEMENTATION
// ============================================================================

struct BTreeNode {
    std::vector<int> keys;
    std::vector<BTreeNode*> children;
    bool isLeaf;

    BTreeNode(bool leaf) : isLeaf(leaf) {}
};

class BTree {
private:
    BTreeNode* root;
    int t; // Minimum degree (defines child bounds [t, 2t] and keys [t-1, 2t-1])

    void searchHelper(BTreeNode* node, int k, BTreeNode*& foundNode, int& idx) {
        int i = 0;
        while (i < node->keys.size() && k > node->keys[i]) {
            i++;
        }
        if (i < node->keys.size() && node->keys[i] == k) {
            foundNode = node;
            idx = i;
            return;
        }
        if (node->isLeaf) {
            foundNode = nullptr;
            idx = -1;
            return;
        }
        searchHelper(node->children[i], k, foundNode, idx);
    }

    void splitChild(BTreeNode* parent, int i, BTreeNode* fullChild) {
        BTreeNode* sibling = new BTreeNode(fullChild->isLeaf);
        
        // Splitting fullChild (2t-1 keys) around index t-1 (median)
        // sibling gets the right t-1 keys of fullChild
        parent->children.insert(parent->children.begin() + i + 1, sibling);
        parent->keys.insert(parent->keys.begin() + i, fullChild->keys[t - 1]);

        sibling->keys.assign(fullChild->keys.begin() + t, fullChild->keys.end());
        fullChild->keys.erase(fullChild->keys.begin() + t - 1, fullChild->keys.end());

        if (!fullChild->isLeaf) {
            sibling->children.assign(fullChild->children.begin() + t, fullChild->children.end());
            fullChild->children.erase(fullChild->children.begin() + t, fullChild->children.end());
        }
    }

    void insertNonFull(BTreeNode* node, int k) {
        int i = node->keys.size() - 1;
        if (node->isLeaf) {
            node->keys.push_back(0);
            while (i >= 0 && node->keys[i] > k) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }
            node->keys[i + 1] = k;
        } else {
            while (i >= 0 && node->keys[i] > k) {
                i--;
            }
            i++;
            if (node->children[i]->keys.size() == 2 * t - 1) {
                splitChild(node, i, node->children[i]);
                if (node->keys[i] < k) {
                    i++;
                }
            }
            insertNonFull(node->children[i], k);
        }
    }

    // Deletion Helpers
    int findKeyIdx(BTreeNode* node, int k) {
        int idx = 0;
        while (idx < node->keys.size() && node->keys[idx] < k) {
            idx++;
        }
        return idx;
    }

    void removeHelper(BTreeNode* node, int k) {
        int idx = findKeyIdx(node, k);

        // Case 1: Key k is present in node and node is leaf
        if (idx < node->keys.size() && node->keys[idx] == k) {
            if (node->isLeaf) {
                node->keys.erase(node->keys.begin() + idx);
            } else {
                removeFromNonLeaf(node, idx);
            }
            return;
        }

        if (node->isLeaf) {
            std::cout << "Key " << k << " does not exist in the B-Tree." << std::endl;
            return;
        }

        // Case 3: Key k is not present in internal node
        bool flag = (idx == node->keys.size());
        if (node->children[idx]->keys.size() < t) {
            fill(node, idx);
        }

        if (flag && idx > node->keys.size()) {
            removeHelper(node->children[idx - 1], k);
        } else {
            removeHelper(node->children[idx], k);
        }
    }

    void removeFromNonLeaf(BTreeNode* node, int idx) {
        int k = node->keys[idx];

        // Case 2a: Left child has at least t keys -> borrow predecessor
        if (node->children[idx]->keys.size() >= t) {
            int pred = getPred(node, idx);
            node->keys[idx] = pred;
            removeHelper(node->children[idx], pred);
        }
        // Case 2b: Right child has at least t keys -> borrow successor
        else if (node->children[idx + 1]->keys.size() >= t) {
            int succ = getSucc(node, idx);
            node->keys[idx] = succ;
            removeHelper(node->children[idx + 1], succ);
        }
        // Case 2c: Both children have t-1 keys -> merge them
        else {
            merge(node, idx);
            removeHelper(node->children[idx], k);
        }
    }

    int getPred(BTreeNode* node, int idx) {
        BTreeNode* curr = node->children[idx];
        while (!curr->isLeaf) {
            curr = curr->children[curr->keys.size()];
        }
        return curr->keys.back();
    }

    int getSucc(BTreeNode* node, int idx) {
        BTreeNode* curr = node->children[idx + 1];
        while (!curr->isLeaf) {
            curr = curr->children[0];
        }
        return curr->keys.front();
    }

    void fill(BTreeNode* node, int idx) {
        if (idx != 0 && node->children[idx - 1]->keys.size() >= t) {
            borrowFromPrev(node, idx);
        } else if (idx != node->keys.size() && node->children[idx + 1]->keys.size() >= t) {
            borrowFromNext(node, idx);
        } else {
            if (idx != node->keys.size()) {
                merge(node, idx);
            } else {
                merge(node, idx - 1);
            }
        }
    }

    void borrowFromPrev(BTreeNode* node, int idx) {
        BTreeNode* child = node->children[idx];
        BTreeNode* sibling = node->children[idx - 1];

        child->keys.insert(child->keys.begin(), node->keys[idx - 1]);

        if (!child->isLeaf) {
            child->children.insert(child->children.begin(), sibling->children.back());
            sibling->children.pop_back();
        }

        node->keys[idx - 1] = sibling->keys.back();
        sibling->keys.pop_back();
    }

    void borrowFromNext(BTreeNode* node, int idx) {
        BTreeNode* child = node->children[idx];
        BTreeNode* sibling = node->children[idx + 1];

        child->keys.push_back(node->keys[idx]);

        if (!child->isLeaf) {
            child->children.push_back(sibling->children.front());
            sibling->children.erase(sibling->children.begin());
        }

        node->keys[idx] = sibling->keys.front();
        sibling->keys.erase(sibling->keys.begin());
    }

    void merge(BTreeNode* node, int idx) {
        BTreeNode* child = node->children[idx];
        BTreeNode* sibling = node->children[idx + 1];

        child->keys.push_back(node->keys[idx]);

        for (int i = 0; i < sibling->keys.size(); i++) {
            child->keys.push_back(sibling->keys[i]);
        }

        if (!child->isLeaf) {
            for (int i = 0; i <= sibling->keys.size(); i++) {
                child->children.push_back(sibling->children[i]);
            }
        }

        node->keys.erase(node->keys.begin() + idx);
        node->children.erase(node->children.begin() + idx + 1);

        delete sibling;
    }

    void printHelper(BTreeNode* node, int level) {
        if (node != nullptr) {
            std::cout << "Level " << level << ": ";
            for (int i = 0; i < node->keys.size(); i++) {
                std::cout << node->keys[i] << " ";
            }
            std::cout << (node->isLeaf ? "[Leaf]" : "[Internal]") << std::endl;
            if (!node->isLeaf) {
                for (int i = 0; i < node->children.size(); i++) {
                    printHelper(node->children[i], level + 1);
                }
            }
        }
    }

    void cleanup(BTreeNode* node) {
        if (node != nullptr) {
            if (!node->isLeaf) {
                for (auto child : node->children) {
                    cleanup(child);
                }
            }
            delete node;
        }
    }

    // Invariants checking
    bool verifyHelper(BTreeNode* node, bool isRoot, int& leafDepth, int currentDepth) {
        if (node == nullptr) return true;

        // Leaf depth check
        if (node->isLeaf) {
            if (leafDepth == -1) {
                leafDepth = currentDepth;
            } else if (leafDepth != currentDepth) {
                std::cerr << "Violation: Leaves are at different depths! " << leafDepth << " vs " << currentDepth << std::endl;
                return false;
            }
        }

        // Minimum key size check
        if (!isRoot) {
            if (node->keys.size() < t - 1) {
                std::cerr << "Violation: Node has too few keys (" << node->keys.size() << " < " << t - 1 << ")" << std::endl;
                return false;
            }
        }

        // Maximum key size check
        if (node->keys.size() > 2 * t - 1) {
            std::cerr << "Violation: Node has too many keys (" << node->keys.size() << " > " << 2 * t - 1 << ")" << std::endl;
            return false;
        }

        // Correct child count check
        if (!node->isLeaf) {
            if (node->children.size() != node->keys.size() + 1) {
                std::cerr << "Violation: Key-Child mismatch! Keys: " << node->keys.size() << ", Children: " << node->children.size() << std::endl;
                return false;
            }
        }

        // Order and sorting checks
        for (size_t i = 0; i < node->keys.size(); i++) {
            if (i > 0 && node->keys[i] <= node->keys[i - 1]) {
                std::cerr << "Violation: Keys are not sorted!" << std::endl;
                return false;
            }
        }

        // Child sub-ranges verification
        if (!node->isLeaf) {
            for (size_t i = 0; i < node->children.size(); i++) {
                BTreeNode* child = node->children[i];
                if (child == nullptr) {
                    std::cerr << "Violation: Null child pointer!" << std::endl;
                    return false;
                }
                
                // Key range checks
                if (i > 0) {
                    for (int key : child->keys) {
                        if (key <= node->keys[i - 1]) {
                            std::cerr << "Violation: Child key " << key << " is smaller than parent bound " << node->keys[i - 1] << std::endl;
                            return false;
                        }
                    }
                }
                if (i < node->keys.size()) {
                    for (int key : child->keys) {
                        if (key >= node->keys[i]) {
                            std::cerr << "Violation: Child key " << key << " is larger than parent bound " << node->keys[i] << std::endl;
                            return false;
                        }
                    }
                }

                if (!verifyHelper(child, false, leafDepth, currentDepth + 1)) {
                    return false;
                }
            }
        }

        return true;
    }

public:
    BTree(int degree) {
        root = nullptr;
        t = degree;
    }

    ~BTree() {
        cleanup(root);
    }

    bool search(int k) {
        BTreeNode* tempNode = nullptr;
        int idx = -1;
        if (root == nullptr) return false;
        searchHelper(root, k, tempNode, idx);
        return tempNode != nullptr;
    }

    void insert(int k) {
        if (root == nullptr) {
            root = new BTreeNode(true);
            root->keys.push_back(k);
        } else {
            if (root->keys.size() == 2 * t - 1) {
                BTreeNode* s = new BTreeNode(false);
                s->children.push_back(root);
                splitChild(s, 0, root);
                int i = 0;
                if (s->keys[0] < k) {
                    i++;
                }
                insertNonFull(s->children[i], k);
                root = s;
            } else {
                insertNonFull(root, k);
            }
        }
    }

    void remove(int k) {
        if (!root) {
            std::cout << "B-Tree is empty." << std::endl;
            return;
        }

        removeHelper(root, k);

        if (root->keys.size() == 0) {
            BTreeNode* tmp = root;
            if (root->isLeaf) {
                root = nullptr;
            } else {
                root = root->children[0];
            }
            delete tmp;
        }
    }

    bool verifyInvariants() {
        if (root == nullptr) return true;
        int leafDepth = -1;
        return verifyHelper(root, true, leafDepth, 0);
    }

    void printTree() {
        if (root == nullptr) {
            std::cout << "Empty Tree" << std::endl;
            return;
        }
        printHelper(root, 0);
    }
};

// ============================================================================
// PART 3: TEST SUITE & INTERACTIVE RUNNER
// ============================================================================

void runRBTTests() {
    std::cout << "\n\033[36m==================================================\033[0m" << std::endl;
    std::cout << "\033[36m             RED-BLACK TREE TEST SUITE             \033[0m" << std::endl;
    std::cout << "\033[36m==================================================\033[0m" << std::endl;

    RedBlackTree rbt;
    std::vector<int> keys = {15, 10, 20, 8, 12, 18, 25, 6, 9, 11, 14, 17, 19, 22, 28};

    std::cout << "[+] Inserting " << keys.size() << " elements sequentially..." << std::endl;
    for (int key : keys) {
        rbt.insert(key);
        assert(rbt.verifyInvariants());
    }
    std::cout << "    RBT Invariants Verified Successfully!" << std::endl;

    std::cout << "\n[+] Current Red-Black Tree Structure:" << std::endl;
    rbt.printTree();

    std::cout << "\n[+] Searching elements..." << std::endl;
    assert(rbt.search(20) != nullptr);
    assert(rbt.search(100) == nullptr);
    std::cout << "    Search tests passed." << std::endl;

    std::cout << "\n[+] Deleting elements sequentially (triggering complex rotation cases)..." << std::endl;
    std::vector<int> deleteKeys = {6, 11, 20, 15, 25};
    for (int key : deleteKeys) {
        std::cout << "    - Deleting key: " << key << std::endl;
        rbt.remove(key);
        assert(rbt.verifyInvariants());
    }
    std::cout << "    Invariants hold perfectly post deletions!" << std::endl;

    std::cout << "\n[+] Tree Structure after deletions:" << std::endl;
    rbt.printTree();
}

void runBTreeTests() {
    std::cout << "\n\033[32m==================================================\033[0m" << std::endl;
    std::cout << "\033[32m               B-TREE TEST SUITE (T=3)             \033[0m" << std::endl;
    std::cout << "\033[32m==================================================\033[0m" << std::endl;

    BTree btree(3); // min degree t=3. Max child nodes = 6, Max keys = 5
    std::vector<int> keys = {10, 20, 5, 6, 12, 30, 7, 17, 22, 25, 27, 35, 40, 45, 50};

    std::cout << "[+] Inserting " << keys.size() << " elements to trigger proactive page splits..." << std::endl;
    for (int key : keys) {
        btree.insert(key);
        assert(btree.verifyInvariants());
    }
    std::cout << "    B-Tree Invariants Verified Successfully!" << std::endl;

    std::cout << "\n[+] Current B-Tree Page Structure:" << std::endl;
    btree.printTree();

    std::cout << "\n[+] Searching elements..." << std::endl;
    assert(btree.search(17) == true);
    assert(btree.search(99) == false);
    std::cout << "    Search tests passed." << std::endl;

    std::cout << "\n[+] Deleting elements sequentially (triggering sibling borrowing & page merging)..." << std::endl;
    std::vector<int> deleteKeys = {6, 12, 30, 10, 35};
    for (int key : deleteKeys) {
        std::cout << "    - Deleting key: " << key << std::endl;
        btree.remove(key);
        assert(btree.verifyInvariants());
    }
    std::cout << "    Invariants hold perfectly post B-Tree page deletions!" << std::endl;

    std::cout << "\n[+] B-Tree Structure after page deletions and consolidations:" << std::endl;
    btree.printTree();
}

int main() {
    std::cout << "==========================================================" << std::endl;
    std::cout << "    LAB 4: RED-BLACK TREE VS B-TREE DESIGN BENCHMARK     " << std::endl;
    std::cout << "    Roll No: 24BCS10239 | Name: Rishi Harti" << std::endl;
    std::cout << "==========================================================" << std::endl;

    runRBTTests();
    runBTreeTests();

    std::cout << "\n\033[92mAll assertions passed successfully! Lab 4 Tree engines are 100% stable.\033[0m\n" << std::endl;
    return 0;
}
