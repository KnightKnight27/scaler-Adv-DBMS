#ifndef BTREE_H
#define BTREE_H

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <iomanip>

class BTreeNode {
public:
    int t;                           // Minimum degree
    std::vector<int> keys;           // Keys stored in the node
    std::vector<std::string> values; // Values corresponding to the keys
    std::vector<BTreeNode*> children; // Child pointers (if not leaf)
    bool isLeaf;                     // True if leaf, false otherwise

    BTreeNode(int degree, bool leaf) : t(degree), isLeaf(leaf) {}

    ~BTreeNode() {
        for (auto child : children) {
            delete child;
        }
    }

    // Traverse and print the subtree rooted at this node
    void print(int indent = 0) const {
        std::string spacing(indent * 4, ' ');
        std::cout << spacing << "[Node] Keys: ";
        std::cout << "[";
        for (size_t i = 0; i < keys.size(); ++i) {
            std::cout << keys[i] << " (val: \"" << values[i] << "\")";
            if (i + 1 < keys.size()) std::cout << ", ";
        }
        std::cout << "] (" << (isLeaf ? "Leaf" : "Internal") << ")\n";

        if (!isLeaf) {
            for (size_t i = 0; i < children.size(); ++i) {
                std::cout << spacing << "  -> Child " << i << ":\n";
                children[i]->print(indent + 1);
            }
        }
    }

    // Search helper: returns pointer to the node containing key and stores path
    BTreeNode* search(int key, std::vector<const BTreeNode*>& path) const {
        path.push_back(this);

        // Find the first key greater than or equal to key
        int i = 0;
        while (i < keys.size() && key > keys[i]) {
            i++;
        }

        // If the found key is equal to key, return this node
        if (i < keys.size() && keys[i] == key) {
            return const_cast<BTreeNode*>(this);
        }

        // If the key is not found here and this is a leaf node, return nullptr
        if (isLeaf) {
            return nullptr;
        }

        // Go to the appropriate child
        return children[i]->search(key, path);
    }

    // Helper for non-full node insertion
    void insertNonFull(int key, const std::string& value) {
        int i = keys.size() - 1;

        if (isLeaf) {
            // Find location to insert and shift keys/values to the right
            keys.resize(keys.size() + 1);
            values.resize(values.size() + 1);

            while (i >= 0 && keys[i] > key) {
                keys[i + 1] = keys[i];
                values[i + 1] = values[i];
                i--;
            }

            // Insert new key and value
            keys[i + 1] = key;
            values[i + 1] = value;
            std::cout << "      [Insertion] Key " << key << " placed in Leaf node at index " << (i + 1) << ".\n";
        } else {
            // Find child which is going to have the new key
            while (i >= 0 && keys[i] > key) {
                i--;
            }
            i++;

            // Check if the found child is full
            if (children[i]->keys.size() == 2 * t - 1) {
                // If the child is full, split it
                splitChild(i, children[i]);

                // After split, the middle key of children[i] moves up and
                // children[i] is split into two. See which of the two
                // is going to have the new key
                if (keys[i] < key) {
                    i++;
                }
            }
            children[i]->insertNonFull(key, value);
        }
    }

    // Split the i-th child of this node (child must be full)
    void splitChild(int i, BTreeNode* y) {
        // Create a new node which is going to store (t-1) keys of y
        BTreeNode* z = new BTreeNode(y->t, y->isLeaf);
        int medianIdx = t - 1;
        int promotedKey = y->keys[medianIdx];
        std::string promotedValue = y->values[medianIdx];

        std::cout << "    >>> [Node Splitting] Node split triggered! <<<\n";
        std::cout << "        Full Node being split (keys): [";
        for (size_t idx = 0; idx < y->keys.size(); ++idx) {
            std::cout << y->keys[idx];
            if (idx + 1 < y->keys.size()) std::cout << ", ";
        }
        std::cout << "]\n";
        std::cout << "        Median key selected for promotion: " << promotedKey << "\n";

        // Copy the last (t-1) keys and values of y to z
        z->keys.resize(t - 1);
        z->values.resize(t - 1);
        for (int j = 0; j < t - 1; j++) {
            z->keys[j] = y->keys[j + t];
            z->values[j] = y->values[j + t];
        }

        // Copy the last t children of y to z
        if (!y->isLeaf) {
            z->children.resize(t);
            for (int j = 0; j < t; j++) {
                z->children[j] = y->children[j + t];
            }
        }

        // Reduce the number of keys in y
        y->keys.resize(t - 1);
        y->values.resize(t - 1);

        // If not leaf, adjust children pointers of y
        if (!y->isLeaf) {
            y->children.resize(t);
        }

        // Insert new child pointer into this node
        children.insert(children.begin() + i + 1, z);

        // Insert key and value promoted from y into this node
        keys.insert(keys.begin() + i, promotedKey);
        values.insert(values.begin() + i, promotedValue);

        std::cout << "        Resulting Nodes after Split:\n";
        std::cout << "          - Left Node:  [";
        for (size_t idx = 0; idx < y->keys.size(); ++idx) {
            std::cout << y->keys[idx];
            if (idx + 1 < y->keys.size()) std::cout << ", ";
        }
        std::cout << "]\n";
        std::cout << "          - Right Node: [";
        for (size_t idx = 0; idx < z->keys.size(); ++idx) {
            std::cout << z->keys[idx];
            if (idx + 1 < z->keys.size()) std::cout << ", ";
        }
        std::cout << "]\n";
        std::cout << "          - Parent keys: [";
        for (size_t idx = 0; idx < keys.size(); ++idx) {
            std::cout << keys[idx];
            if (idx + 1 < keys.size()) std::cout << ", ";
        }
        std::cout << "]\n";
    }
};

class BTree {
private:
    BTreeNode* root;
    int t; // Minimum degree

public:
    BTree(int degree) : root(nullptr), t(degree) {}

    ~BTree() {
        delete root;
    }

    void printTree() const {
        if (root != nullptr) {
            root->print();
        } else {
            std::cout << "[Empty Tree]\n";
        }
    }

    // Search key in tree
    std::pair<std::string, bool> search(int key, std::vector<const BTreeNode*>& path) const {
        path.clear();
        if (root == nullptr) {
            return {"", false};
        }
        BTreeNode* result = root->search(key, path);
        if (result != nullptr) {
            // Find key index in results
            for (size_t i = 0; i < result->keys.size(); ++i) {
                if (result->keys[i] == key) {
                    return {result->values[i], true};
                }
            }
        }
        return {"", false};
    }

    // Insert key-value pair
    void insert(int key, const std::string& value) {
        std::cout << "  * Inserting key " << key << " (\"" << value << "\")...\n";
        if (root == nullptr) {
            root = new BTreeNode(t, true);
            root->keys.push_back(key);
            root->values.push_back(value);
            std::cout << "      [Insertion] Created root node. Placed key " << key << ".\n";
            return;
        }

        // If root is full, tree grows in height
        if (root->keys.size() == 2 * t - 1) {
            std::cout << "    >>> [Root Split] Root is full! Height increases. <<<\n";
            BTreeNode* s = new BTreeNode(t, false);
            s->children.push_back(root);

            // Split the old root and promote key to new root s
            s->splitChild(0, root);

            // Decide which child will have the new key
            int i = 0;
            if (s->keys[0] < key) {
                i++;
            }
            s->children[i]->insertNonFull(key, value);

            root = s;
        } else {
            root->insertNonFull(key, value);
        }
    }

    void printMetadata() const {
        std::cout << "===========================================\n";
        std::cout << " B-Tree Properties (Degree t = " << t << ")\n";
        std::cout << "===========================================\n";
        std::cout << " - Min degree (t): " << t << "\n";
        std::cout << " - Max number of child pointers: " << 2 * t << "\n";
        std::cout << " - Max keys per node (2t - 1): " << (2 * t - 1) << "\n";
        std::cout << " - Min keys per node (t - 1): " << (t - 1) << " (except root)\n";
        std::cout << "===========================================\n\n";
    }
};

#endif // BTREE_H
