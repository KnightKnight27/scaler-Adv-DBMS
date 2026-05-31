#include <iostream>
#include <climits>
#include <vector>

// B-Tree where each node stores keys and pointers to children
// [  [] 10|ROW_OF_10 [] 20|ROW_OF_20 [] 40|ROW_OF_40 [] ]

template <typename Key, typename Row>
class DB {
private:
    struct Entry {
        Key key;
        Row row;
    };

    struct BTree {
        std::vector<Entry>  keys;
        std::vector<BTree*> children;
        bool isLeaf;
        BTree() : isLeaf(true) {}
    };

    BTree  *root;
    size_t  t;       // minimum degree: each node has at most 2t-1 keys

    bool isFull(BTree *node) {
        return node->keys.size() == 2 * t - 1;
    }

    // search subtree rooted at node for key
    Entry* searchNode(BTree *node, Key key) {
        size_t i = 0;
        while (i < node->keys.size() && key > node->keys[i].key)
            i++;

        if (i < node->keys.size() && key == node->keys[i].key)
            return &node->keys[i];

        if (node->isLeaf)
            return nullptr;

        return searchNode(node->children[i], key);
    }

    // split full child at index i of parent
    void splitChild(BTree *parent, int i) {
        BTree *full    = parent->children[i];
        BTree *newNode = new BTree();
        newNode->isLeaf = full->isLeaf;

        int mid = t - 1; // index of middle key

        // right half of keys go to newNode
        for (size_t j = mid + 1; j < 2 * t - 1; j++)
            newNode->keys.push_back(full->keys[j]);

        // right half of children go to newNode (if not leaf)
        if (!full->isLeaf) {
            for (size_t j = t; j < 2 * t; j++)
                newNode->children.push_back(full->children[j]);
            full->children.resize(t);
        }

        // middle key moves up to parent
        Entry midKey = full->keys[mid];
        full->keys.resize(mid);

        parent->keys.insert(parent->keys.begin() + i, midKey);
        parent->children.insert(parent->children.begin() + i + 1, newNode);
    }

    // insert key into a node that is guaranteed not full
    void insertNonFull(BTree *node, Key key, Row row) {
        int i = node->keys.size() - 1;

        if (node->isLeaf) {
            node->keys.push_back({key, row});
            while (i >= 0 && node->keys[i].key > key) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }
            node->keys[i + 1] = {key, row};
        } else {
            while (i >= 0 && node->keys[i].key > key)
                i--;
            i++;
            if (isFull(node->children[i])) {
                splitChild(node, i);
                if (node->keys[i].key < key)
                    i++;
            }
            insertNonFull(node->children[i], key, row);
        }
    }

public:
    DB(size_t degree) : t(degree) {
        root = new BTree();
    }

    Entry* search(Key key) {
        return searchNode(root, key);
    }

    void insert(Key key, Row row) {
        if (isFull(root)) {
            BTree *newRoot    = new BTree();
            newRoot->isLeaf   = false;
            newRoot->children.push_back(root);
            splitChild(newRoot, 0);
            root = newRoot;
        }
        insertNonFull(root, key, row);
    }
};

int main() {
    DB<int, std::string> db(2); // minimum degree 2 → max 3 keys per node

    db.insert(10, "row_10");
    db.insert(20, "row_20");
    db.insert(5,  "row_5");
    db.insert(15, "row_15");
    db.insert(30, "row_30");
    db.insert(40, "row_40");

    auto e = db.search(15);
    std::cout << "search 15: " << (e ? e->row : "not found") << "\n";

    e = db.search(99);
    std::cout << "search 99: " << (e ? e->row : "not found") << "\n";
}
