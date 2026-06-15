#include <iostream>
#include <vector>
#include <string>
using namespace std;

template <typename Key, typename Row>
class DB {
public:
    struct Entry {
        Key key;
        Row row;
    };

private:
    struct BTree {
        vector<Entry> keys;
        vector<BTree*> children;
        bool isLeaf;
        BTree(bool leaf) : isLeaf(leaf) {}
    };

    BTree* root;
    size_t minDegree;
    size_t maxKeys;

    bool isFull(BTree* node) {
        return node->keys.size() == maxKeys;
    }

    void SplitChild(BTree* parent, size_t idx) {
        BTree* child = parent->children[idx];
        BTree* sibling = new BTree(child->isLeaf);
        size_t t = minDegree;

        for (size_t j = 0; j < t - 1; j++)
            sibling->keys.push_back(child->keys[t + j]);

        if (!child->isLeaf)
            for (size_t j = 0; j < t; j++)
                sibling->children.push_back(child->children[t + j]);

        Entry median = child->keys[t - 1];
        child->keys.resize(t - 1);
        if (!child->isLeaf)
            child->children.resize(t);

        parent->children.insert(parent->children.begin() + idx + 1, sibling);
        parent->keys.insert(parent->keys.begin() + idx, median);
    }

    void InsertNonFull(BTree* node, Key key, Row row) {
        int i = (int)node->keys.size() - 1;

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
                SplitChild(node, i);
                if (node->keys[i].key < key)
                    i++;
            }
            InsertNonFull(node->children[i], key, row);
        }
    }

    Entry SearchRecursive(Key key, BTree* node) {
        if (node == nullptr)
            return Entry{Key(), Row()};

        size_t i = 0;
        while (i < node->keys.size() && key > node->keys[i].key)
            i++;

        if (i < node->keys.size() && key == node->keys[i].key)
            return node->keys[i];

        if (node->isLeaf)
            return Entry{Key(), Row()};

        return SearchRecursive(key, node->children[i]);
    }

    void PrintInOrder(BTree* node) {
        if (!node) return;
        for (size_t i = 0; i < node->keys.size(); i++) {
            if (!node->isLeaf)
                PrintInOrder(node->children[i]);
            cout << "[" << node->keys[i].key << "|" << node->keys[i].row << "] ";
        }
        if (!node->isLeaf)
            PrintInOrder(node->children[node->keys.size()]);
    }

public:
    DB(size_t degree) {
        minDegree = degree;
        maxKeys = 2 * degree - 1;
        root = new BTree(true);
    }

    Entry Search(Key key) {
        return SearchRecursive(key, root);
    }

    void Insert(Key key, Row row) {
        if (isFull(root)) {
            BTree* newRoot = new BTree(false);
            newRoot->children.push_back(root);
            root = newRoot;
            SplitChild(root, 0);
        }
        InsertNonFull(root, key, row);
    }

    void Display() {
        PrintInOrder(root);
        cout << endl;
    }
};

int main() {
    DB<int, string> db(3);

    db.Insert(10, "ROW_OF_10");
    db.Insert(20, "ROW_OF_20");
    db.Insert(5, "ROW_OF_5");
    db.Insert(15, "ROW_OF_15");
    db.Insert(25, "ROW_OF_25");
    db.Insert(30, "ROW_OF_30");
    db.Insert(40, "ROW_OF_40");

    cout << "B-Tree: ";
    db.Display();

    auto res = db.Search(20);
    cout << "Search(20): " << res.row << endl;

    res = db.Search(99);
    cout << "Search(99): " << res.row << endl;

    return 0;
}
