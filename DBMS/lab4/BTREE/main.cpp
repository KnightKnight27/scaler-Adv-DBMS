
#include <iostream>
#include <vector>
#include <algorithm>

template <typename Key, typename Row>
class DB {
private:
    struct Entry {
        Key key;
        Row row;
    };

    struct BTree {
        bool leaf;
        std::vector<Entry> keys;
        std::vector<BTree*> children;

        BTree(bool isLeaf = true) : leaf(isLeaf) {}
    };

    BTree* root;
    size_t minDegree;
    size_t maxDegree;
    size_t minKeys;
    size_t maxKeys;

    bool isFull(BTree* node) {
        return node->keys.size() == maxKeys;
    }

    Entry* SearchRecursive(BTree* node, const Key& key) {
        if (!node) return nullptr;

        int i = 0;
        while (i < (int)node->keys.size() && key > node->keys[i].key)
            i++;

        if (i < (int)node->keys.size() && key == node->keys[i].key)
            return &node->keys[i];

        if (node->leaf)
            return nullptr;

        return SearchRecursive(node->children[i], key);
    }

    void splitChild(BTree* parent, int index) {
        BTree* fullChild = parent->children[index];
        BTree* newNode = new BTree(fullChild->leaf);

        Entry middle = fullChild->keys[minDegree - 1];

        for (size_t j = minDegree; j < fullChild->keys.size(); j++)
            newNode->keys.push_back(fullChild->keys[j]);

        fullChild->keys.resize(minDegree - 1);

        if (!fullChild->leaf) {
            for (size_t j = minDegree; j < fullChild->children.size(); j++)
                newNode->children.push_back(fullChild->children[j]);

            fullChild->children.resize(minDegree);
        }

        parent->children.insert(parent->children.begin() + index + 1, newNode);
        parent->keys.insert(parent->keys.begin() + index, middle);
    }

    void insertNonFull(BTree* node, const Key& key, const Row& row) {
        int i = (int)node->keys.size() - 1;

        if (node->leaf) {
            Entry entry{key, row};
            node->keys.push_back(entry);

            while (i >= 0 && node->keys[i].key > key) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }

            node->keys[i + 1] = entry;
        } else {
            while (i >= 0 && key < node->keys[i].key)
                i--;

            i++;

            if (isFull(node->children[i])) {
                splitChild(node, i);

                if (key > node->keys[i].key)
                    i++;
            }

            insertNonFull(node->children[i], key, row);
        }
    }

    void printRecursive(BTree* node, int level) {
        if (!node) return;

        std::cout << "Level " << level << ": ";
        for (auto& e : node->keys)
            std::cout << e.key << " ";
        std::cout << "\n";

        for (auto child : node->children)
            printRecursive(child, level + 1);
    }

public:
    DB(size_t degree) {
        minDegree = degree;
        maxDegree = 2 * degree;
        minKeys = degree - 1;
        maxKeys = 2 * degree - 1;

        root = new BTree(true);
    }

    bool Search(const Key& key, Row& result) {
        Entry* found = SearchRecursive(root, key);

        if (!found)
            return false;

        result = found->row;
        return true;
    }

    void Insert(const Key& key, const Row& row) {
        if (isFull(root)) {
            BTree* newRoot = new BTree(false);
            newRoot->children.push_back(root);

            splitChild(newRoot, 0);

            root = newRoot;
        }

        insertNonFull(root, key, row);
    }

    void Print() {
        printRecursive(root, 0);
    }
};

int main() {
    DB<int, std::string> db(2);

    db.Insert(10, "Row10");
    db.Insert(20, "Row20");
    db.Insert(5, "Row5");
    db.Insert(6, "Row6");
    db.Insert(12, "Row12");
    db.Insert(30, "Row30");
    db.Insert(7, "Row7");
    db.Insert(17, "Row17");

    db.Print();

    std::string row;
    if (db.Search(12, row))
        std::cout << "Found: " << row << std::endl;
    else
        std::cout << "Not Found\n";

    return 0;
}
