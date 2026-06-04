#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

template<typename Key, typename Value>
class BTree {
private:
    struct BTreeNode {
        bool isLeaf;
        std::vector<std::pair<Key, Value>> keys; // Sorted key-value pairs
        std::vector<BTreeNode*> children;         // Pointers to child nodes

        explicit BTreeNode(bool leaf) : isLeaf(leaf) {}
    };

    BTreeNode* root;
    int t; // Minimum degree of the B-Tree

    void splitChild(BTreeNode* parent, int i, BTreeNode* child) {
        std::cout << "[SPLIT] Node is full. Splitting Child Node (at index " << i << " of parent).\n";
        
        // Create a new node to store (t-1) keys of child
        BTreeNode* z = new BTreeNode(child->isLeaf);
        
        // Find the median key index
        int medianIndex = t - 1;
        std::pair<Key, Value> medianKV = child->keys[medianIndex];
        std::cout << " -> Median key selected for promotion: " << medianKV.first << "\n";

        // Distribute the keys of child to z
        z->keys.assign(child->keys.begin() + t, child->keys.end());
        child->keys.erase(child->keys.begin() + medianIndex, child->keys.end());

        // Distribute children if node is not leaf
        if (!child->isLeaf) {
            z->children.assign(child->children.begin() + t, child->children.end());
            child->children.erase(child->children.begin() + t, child->children.end());
        }

        // Insert z pointer into parent's children list
        parent->children.insert(parent->children.begin() + i + 1, z);

        // Promote the median key to parent
        parent->keys.insert(parent->keys.begin() + i, medianKV);

        std::cout << " -> Promotion complete. Node height adjusted, records redistributed.\n";
    }

    void insertNonFull(BTreeNode* node, const Key& key, const Value& value) {
        int i = node->keys.size() - 1;

        if (node->isLeaf) {
            // Find key position and insert in sorted order
            node->keys.push_back({key, value});
            while (i >= 0 && node->keys[i].first > key) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }
            node->keys[i + 1] = {key, value};
            std::cout << " -> Leaf insertion: Key " << key << " placed in sorted position.\n";
        } else {
            // Find child index where key fits
            while (i >= 0 && node->keys[i].first > key) {
                i--;
            }
            i++;

            // Split child if it's full (capacity = 2t - 1)
            if (node->children[i]->keys.size() == (2 * t - 1)) {
                splitChild(node, i, node->children[i]);

                // Determine which child should receive key after split promotion
                if (node->keys[i].first < key) {
                    i++;
                }
            }
            insertNonFull(node->children[i], key, value);
        }
    }

    std::pair<BTreeNode*, int> searchHelper(BTreeNode* node, const Key& key) const {
        int i = 0;
        std::cout << "Analyzing Node [Keys: ";
        for (const auto& kv : node->keys) {
            std::cout << kv.first << " ";
        }
        std::cout << "] -> ";

        while (i < node->keys.size() && key > node->keys[i].first) {
            i++;
        }

        if (i < node->keys.size() && key == node->keys[i].first) {
            std::cout << "KEY FOUND!\n";
            return {node, i};
        }

        if (node->isLeaf) {
            std::cout << "reached leaf, KEY NOT FOUND.\n";
            return {nullptr, -1};
        }

        std::cout << "descending to child index " << i << "\n";
        return searchHelper(node->children[i], key);
    }

    void traverseHelper(BTreeNode* node) const {
        int i;
        for (i = 0; i < node->keys.size(); i++) {
            if (!node->isLeaf) {
                traverseHelper(node->children[i]);
            }
            std::cout << "[" << node->keys[i].first << " => " << node->keys[i].second << "] ";
        }
        if (!node->isLeaf) {
            traverseHelper(node->children[i]);
        }
    }

    void printTreeHelper(BTreeNode* node, int level) const {
        if (node != nullptr) {
            std::cout << "Level " << level << ": ";
            for (const auto& kv : node->keys) {
                std::cout << kv.first << ":" << kv.second << " ";
            }
            std::cout << std::endl;
            if (!node->isLeaf) {
                for (auto* child : node->children) {
                    printTreeHelper(child, level + 1);
                }
            }
        }
    }

public:
    explicit BTree(int degree) : root(nullptr), t(degree) {
        if (degree < 2) {
            throw std::invalid_argument("Minimum degree must be at least 2");
        }
        std::cout << "[INIT] B-Tree initialized with minimum degree (t) = " << t << "\n";
        std::cout << " -> Node capacity limits: Min keys = " << (t - 1) 
                  << ", Max keys = " << (2 * t - 1) << "\n\n";
    }

    void insert(const Key& key, const Value& value) {
        std::cout << "\n[INSERT START] Inserting Key-Value Pair (" << key << " => " << value << ")\n";
        
        if (root == nullptr) {
            // First node creation
            root = new BTreeNode(true);
            root->keys.push_back({key, value});
            std::cout << " -> Allocated root node. Key " << key << " placed as initial key.\n";
            return;
        }

        // If root is full, split and grow the tree depth
        if (root->keys.size() == (2 * t - 1)) {
            std::cout << "[ROOT FULL] Splitting old root, tree depth increases.\n";
            BTreeNode* s = new BTreeNode(false);
            s->children.push_back(root);
            
            // Split child at index 0 of new root s
            splitChild(s, 0, root);

            // Set s as the new root
            root = s;

            // Determine which split branch gets the new key
            int i = 0;
            if (s->keys[0].first < key) {
                i++;
            }
            insertNonFull(s->children[i], key, value);
        } else {
            insertNonFull(root, key, value);
        }
    }

    void search(const Key& key) const {
        std::cout << "\n[SEARCH] Searching for Key: " << key << "\n ";
        if (root == nullptr) {
            std::cout << "Tree is empty.\n";
            return;
        }
        auto result = searchHelper(root, key);
        if (result.first != nullptr) {
            std::cout << " -> Key: " << key << " has Value: " << result.first->keys[result.second].second << "\n";
        } else {
            std::cout << " -> Key not found.\n";
        }
    }

    void traverse() const {
        std::cout << "\n[TRAVERSAL] Inorder Traversal:\n ";
        if (root != nullptr) {
            traverseHelper(root);
        }
        std::cout << "\n";
    }

    void displayStructure() const {
        std::cout << "\n------------------ TREE STRUCTURE ------------------\n";
        if (root == nullptr) {
            std::cout << "Empty Tree\n";
        } else {
            printTreeHelper(root, 0);
        }
        std::cout << "----------------------------------------------------\n\n";
    }
};

int main() {
    std::cout << "========================================================\n";
    std::cout << "      Lab 6: B-Tree Indexing Structure Demo            \n";
    std::cout << "========================================================\n\n";

    // Task 1: B-Tree Initialization (degree t = 2)
    // Max keys = 2*2 - 1 = 3 keys. Splits on 4th insert.
    BTree<int, std::string> btree(2);
    btree.displayStructure();

    // Task 2: Record Insertion & sorted order
    btree.insert(10, "Record_10");
    btree.insert(20, "Record_20");
    btree.insert(30, "Record_30");
    btree.displayStructure();

    // Task 3: Node Splitting on inserting the 4th key (40)
    btree.insert(40, "Record_40");
    btree.displayStructure();

    btree.insert(50, "Record_50");
    btree.insert(60, "Record_60"); // triggers another split
    btree.displayStructure();

    // Task 4: Search Operations
    btree.search(30); // Existing key
    btree.search(95); // Non-existing key

    // Task 5: Traversal
    btree.traverse();

    std::cout << "\n========================================================\n";
    std::cout << "      Lab 6 execution completed successfully!          \n";
    std::cout << "========================================================\n";

    return 0;
}
