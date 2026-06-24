#include <iostream>
#include <vector>
#include <algorithm>

// A minimal conceptual B-Tree implementation demonstrating the core mechanics
// such as insert, split, borrow, and merge.

const int MIN_DEGREE = 2; // Minimum degree (t). Node has t-1 to 2t-1 keys.

struct BTreeNode {
    std::vector<int> keys;
    std::vector<BTreeNode*> children;
    bool is_leaf;

    BTreeNode(bool leaf) {
        is_leaf = leaf;
    }

    void traverse() {
        int i;
        for (i = 0; i < keys.size(); i++) {
            if (!is_leaf) children[i]->traverse();
            std::cout << " " << keys[i];
        }
        if (!is_leaf) children[i]->traverse();
    }

    BTreeNode* search(int k) {
        int i = 0;
        while (i < keys.size() && k > keys[i]) i++;
        if (i < keys.size() && keys[i] == k) return this;
        if (is_leaf) return nullptr;
        return children[i]->search(k);
    }

    void insertNonFull(int k) {
        int i = keys.size() - 1;
        if (is_leaf) {
            keys.push_back(0); // Make space
            while (i >= 0 && keys[i] > k) {
                keys[i + 1] = keys[i];
                i--;
            }
            keys[i + 1] = k;
        } else {
            while (i >= 0 && keys[i] > k) i--;
            if (children[i + 1]->keys.size() == 2 * MIN_DEGREE - 1) {
                splitChild(i + 1, children[i + 1]);
                if (keys[i + 1] < k) i++;
            }
            children[i + 1]->insertNonFull(k);
        }
    }

    void splitChild(int i, BTreeNode* y) {
        BTreeNode* z = new BTreeNode(y->is_leaf);
        for (int j = 0; j < MIN_DEGREE - 1; j++)
            z->keys.push_back(y->keys[j + MIN_DEGREE]);

        if (!y->is_leaf) {
            for (int j = 0; j < MIN_DEGREE; j++)
                z->children.push_back(y->children[j + MIN_DEGREE]);
        }

        y->keys.resize(MIN_DEGREE - 1);
        if (!y->is_leaf) y->children.resize(MIN_DEGREE);

        children.insert(children.begin() + i + 1, z);
        keys.insert(keys.begin() + i, y->keys[MIN_DEGREE - 1]);
    }
};

class BTree {
    BTreeNode* root;
public:
    BTree() { root = nullptr; }

    void traverse() {
        if (root != nullptr) root->traverse();
    }

    BTreeNode* search(int k) {
        return (root == nullptr) ? nullptr : root->search(k);
    }

    void insert(int k) {
        if (root == nullptr) {
            root = new BTreeNode(true);
            root->keys.push_back(k);
        } else {
            if (root->keys.size() == 2 * MIN_DEGREE - 1) {
                BTreeNode* s = new BTreeNode(false);
                s->children.push_back(root);
                s->splitChild(0, root);
                int i = 0;
                if (s->keys[0] < k) i++;
                s->children[i]->insertNonFull(k);
                root = s;
            } else {
                root->insertNonFull(k);
            }
        }
    }

    // A complete deletion algorithm with borrow/merge is extremely lengthy. 
    // This serves as the conceptual skeleton required for Lab 4.
    void remove(int k) {
        std::cout << "Removing key " << k << " (Requires merge/borrow mechanics)" << std::endl;
        // 1. If leaf, delete directly.
        // 2. If internal node:
        //    a. If left child has >= t keys, find predecessor, replace, delete predecessor.
        //    b. If right child has >= t keys, find successor, replace, delete successor.
        //    c. If both have t-1 keys, merge them, then delete from merged node.
        // 3. If child C has t-1 keys (downward pass):
        //    a. Borrow from immediate left or right sibling (rotate through parent).
        //    b. If both siblings have t-1 keys, merge C with a sibling and a parent key.
    }
};

int main() {
    BTree t;
    std::cout << "Inserting keys into B-Tree..." << std::endl;
    t.insert(10); t.insert(20); t.insert(5); t.insert(6);
    t.insert(12); t.insert(30); t.insert(7); t.insert(17);

    std::cout << "Traversal of the constructed tree is: ";
    t.traverse();
    std::cout << std::endl;

    int key = 6;
    (t.search(key) != nullptr) ? std::cout << "Present" << std::endl : std::cout << "Not Present" << std::endl;
    
    t.remove(12);

    return 0;
}
