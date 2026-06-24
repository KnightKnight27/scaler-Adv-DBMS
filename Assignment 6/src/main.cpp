#include <iostream>
#include <vector>
#include <string>

using namespace std;

template<typename Key, typename Row>
class BTreeHelper {
private:
    struct Entry {
        Key key;
        Row row;
    };

    struct BTreeNode {
        vector<Entry> keys;
        vector<BTreeNode*> children;
        bool isLeaf;

        BTreeNode(bool leaf = true)
            : isLeaf(leaf) {}
    };

    BTreeNode* root;

    size_t minDegree;
    size_t maxKeys;

private:

    void destroy(BTreeNode* node) {
        if (node == nullptr)
            return;

        for (auto child : node->children) {
            destroy(child);
        }

        delete node;
    }

    Entry* searchNode(BTreeNode* node, const Key& key) {

        size_t i = 0;

        while (i < node->keys.size() &&
               key > node->keys[i].key) {
            i++;
        }

        if (i < node->keys.size() &&
            key == node->keys[i].key) {
            return &node->keys[i];
        }

        if (node->isLeaf) {
            return nullptr;
        }

        return searchNode(node->children[i], key);
    }

    void splitChild(BTreeNode* parent,
                    size_t childIndex) {

        BTreeNode* fullChild =
            parent->children[childIndex];

        BTreeNode* newNode =
            new BTreeNode(fullChild->isLeaf);

        size_t t = minDegree;

        Entry middleKey =
            fullChild->keys[t - 1];

        for (size_t i = t;
             i < fullChild->keys.size();
             i++) {
            newNode->keys.push_back(
                fullChild->keys[i]
            );
        }

        fullChild->keys.resize(t - 1);

        if (!fullChild->isLeaf) {

            for (size_t i = t;
                 i < fullChild->children.size();
                 i++) {
                newNode->children.push_back(
                    fullChild->children[i]
                );
            }

            fullChild->children.resize(t);
        }

        parent->children.insert(
            parent->children.begin()
            + childIndex + 1,
            newNode
        );

        parent->keys.insert(
            parent->keys.begin()
            + childIndex,
            middleKey
        );
    }

    void insertNonFull(BTreeNode* node,
                       const Key& key,
                       const Row& row) {

        int i =
            static_cast<int>(node->keys.size()) - 1;

        if (node->isLeaf) {

            Entry newEntry{key, row};

            node->keys.push_back(newEntry);

            while (i >= 0 &&
                   key < node->keys[i].key) {

                node->keys[i + 1] =
                    node->keys[i];

                i--;
            }

            node->keys[i + 1] = newEntry;
        }
        else {

            while (i >= 0 &&
                   key < node->keys[i].key) {
                i--;
            }

            i++;

            if (node->children[i]->keys.size()
                == maxKeys) {

                splitChild(node, i);

                if (key >
                    node->keys[i].key) {
                    i++;
                }
            }

            insertNonFull(
                node->children[i],
                key,
                row
            );
        }
    }

    void printNode(BTreeNode* node,
                   int level) {

        cout << "Level "
             << level
             << ": ";

        for (auto& entry : node->keys) {
            cout << entry.key << " ";
        }

        cout << endl;

        if (!node->isLeaf) {
            for (auto child : node->children) {
                printNode(child,
                          level + 1);
            }
        }
    }

public:

    BTreeHelper(size_t degree)
        : root(new BTreeNode(true)),
          minDegree(degree),
          maxKeys(2 * degree - 1) {}

    ~BTreeHelper() {
        destroy(root);
    }

    Entry* search(const Key& key) {
        return searchNode(root, key);
    }

    void insert(const Key& key,
                const Row& row) {

        if (search(key) != nullptr) {
            cout << "Key "
                 << key
                 << " already exists\n";
            return;
        }

        if (root->keys.size() ==
            maxKeys) {

            BTreeNode* newRoot =
                new BTreeNode(false);

            newRoot->children.push_back(root);

            splitChild(newRoot, 0);

            root = newRoot;
        }

        insertNonFull(root,
                      key,
                      row);
    }

    void print() {
        cout << "\nB-Tree Structure\n";
        cout << "================\n";

        printNode(root, 0);

        cout << endl;
    }
};

int main() {

    BTreeHelper<int, string> tree(3);

    tree.insert(10, "A");
    tree.insert(20, "B");
    tree.insert(5, "C");
    tree.insert(6, "D");
    tree.insert(12, "E");
    tree.insert(30, "F");
    tree.insert(7, "G");
    tree.insert(17, "H");

    tree.print();

    auto result = tree.search(12);

    if (result != nullptr) {
        cout << "Found: "
             << result->key
             << " -> "
             << result->row
             << endl;
    }
    else {
        cout << "Key not found\n";
    }

    result = tree.search(100);

    if (result != nullptr) {
        cout << "Found: "
             << result->key
             << " -> "
             << result->row
             << endl;
    }
    else {
        cout << "Key 100 not found\n";
    }

    return 0;
}