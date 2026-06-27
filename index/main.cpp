#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

template <typename Key, typename Row>
class DB {
public:
    struct Entry {
        Key key;
        Row row;
        Entry() : key(Key{}), row(Row{}) {}
        Entry(Key k, Row r) : key(k), row(r) {}
    };

private:
    struct BTreeNode {
        std::vector<Entry> keys;
        std::vector<BTreeNode*> children;
        bool isLeaf;

        BTreeNode(bool leaf) : isLeaf(leaf) {}
        ~BTreeNode() {
            for (auto child : children) {
                delete child;
            }
        }
    };

    BTreeNode* root;
    size_t t; // Minimum degree (defines range for number of keys)

    // Helper for searching
    Entry* searchRecursive(BTreeNode* node, Key key) {
        size_t i = 0;
        while (i < node->keys.size() && key > node->keys[i].key) {
            i++;
        }

        if (i < node->keys.size() && node->keys[i].key == key) {
            return &node->keys[i];
        }

        if (node->isLeaf) {
            return nullptr;
        }

        return searchRecursive(node->children[i], key);
    }

    // Helper to split a full child node
    void splitChild(BTreeNode* parent, size_t i, BTreeNode* child) {
        BTreeNode* z = new BTreeNode(child->isLeaf);
        
        // child currently has 2*t - 1 keys. We take the middle key (index t-1).
        Entry middleEntry = child->keys[t - 1];

        // Move keys from index t to 2*t-2 (t-1 keys) to z
        z->keys.assign(child->keys.begin() + t, child->keys.end());
        
        if (!child->isLeaf) {
            // Move children from index t to 2*t-1 (t children) to z
            z->children.assign(child->children.begin() + t, child->children.end());
        }

        // Reduce keys in child
        child->keys.resize(t - 1);
        if (!child->isLeaf) {
            child->children.resize(t);
        }

        // Insert new child pointer in parent
        parent->children.insert(parent->children.begin() + i + 1, z);

        // Insert middle key in parent
        parent->keys.insert(parent->keys.begin() + i, middleEntry);
    }

    void insertNonFull(BTreeNode* node, Key key, Row row) {
        int i = node->keys.size() - 1;

        if (node->isLeaf) {
            // Insert the key in sorted order
            node->keys.push_back(Entry(key, row));
            while (i >= 0 && node->keys[i].key > key) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }
            node->keys[i + 1] = Entry(key, row);
        } else {
            // Find child that should receive the key
            while (i >= 0 && node->keys[i].key > key) {
                i--;
            }
            i++;

            if (node->children[i]->keys.size() == 2 * t - 1) {
                splitChild(node, i, node->children[i]);
                if (key > node->keys[i].key) {
                    i++;
                }
            }
            insertNonFull(node->children[i], key, row);
        }
    }

    void printRecursive(BTreeNode* node, int depth) {
        std::string indent(depth * 4, ' ');
        std::cout << indent << "[ ";
        for (const auto& entry : node->keys) {
            std::cout << entry.key << ":" << entry.row << " ";
        }
        std::cout << "]\n";
        if (!node->isLeaf) {
            for (auto child : node->children) {
                printRecursive(child, depth + 1);
            }
        }
    }

public:
    DB(size_t degree) : t(degree), root(nullptr) {
        if (t < 2) t = 2; // B-tree minimum degree must be at least 2
    }
    ~DB() {
        delete root;
    }

    void Insert(Key key, Row row) {
        if (root == nullptr) {
            root = new BTreeNode(true);
            root->keys.push_back(Entry(key, row));
            return;
        }

        if (root->keys.size() == 2 * t - 1) {
            BTreeNode* s = new BTreeNode(false);
            s->children.push_back(root);
            splitChild(s, 0, root);

            int i = 0;
            if (key > s->keys[0].key) {
                i++;
            }
            insertNonFull(s->children[i], key, row);
            root = s;
        } else {
            insertNonFull(root, key, row);
        }
    }

    bool Search(Key key, Row& resultRow) {
        if (root == nullptr) return false;
        Entry* found = searchRecursive(root, key);
        if (found) {
            resultRow = found->row;
            return true;
        }
        return false;
    }

    void printTree() {
        if (root == nullptr) {
            std::cout << "Empty Index\n";
            return;
        }
        printRecursive(root, 0);
    }
};

int main() {
    std::cout << "Initializing B-Tree Index with degree 2...\n";
    DB<int, std::string> db(2);

    std::cout << "Inserting keys...\n";
    db.Insert(10, "Record_10");
    db.Insert(20, "Record_20");
    db.Insert(5, "Record_5");
    db.Insert(15, "Record_15");
    db.Insert(30, "Record_30");
    db.Insert(25, "Record_25");

    std::cout << "\nB-Tree structure:\n";
    db.printTree();

    std::cout << "\nSearching for keys:\n";
    for (int key : {15, 25, 40}) {
        std::string row;
        if (db.Search(key, row)) {
            std::cout << "  Found key " << key << ": " << row << "\n";
        } else {
            std::cout << "  Key " << key << " not found.\n";
        }
    }
    return 0;
}
