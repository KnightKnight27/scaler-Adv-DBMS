#include <iostream>
#include <vector>
#include <string>

template <class Key, class Data>
class BTreeDB {
private:

    struct Entry {
        Key key;
        Data data;
    };

    struct Page {
        bool isLeaf;
        std::vector<Entry> entries;
        std::vector<Page*> children;

        Page(bool leaf = true) {
            isLeaf = leaf;
        }
    };

    Page* root;
    int minDegree;

private:

    void splitChild(Page* parent, int idx) {

        Page* left = parent->children[idx];
        Page* right = new Page(left->isLeaf);

        int mid = minDegree - 1;

        // move keys to new node
        for (int i = minDegree; i < left->entries.size(); i++) {
            right->entries.push_back(left->entries[i]);
        }

        // move child pointers if internal node
        if (!left->isLeaf) {
            for (int i = minDegree; i < left->children.size(); i++) {
                right->children.push_back(left->children[i]);
            }
        }

        Entry promoted = left->entries[mid];

        left->entries.resize(mid);

        if (!left->isLeaf) {
            left->children.resize(minDegree);
        }

        parent->entries.insert(parent->entries.begin() + idx, promoted);
        parent->children.insert(parent->children.begin() + idx + 1, right);
    }

    void insertNonFull(Page* node, const Key& key, const Data& value) {

        if (node->isLeaf) {

            Entry temp{key, value};

            int i = node->entries.size() - 1;

            node->entries.push_back(temp);

            while (i >= 0 && node->entries[i].key > key) {
                node->entries[i + 1] = node->entries[i];
                i--;
            }

            node->entries[i + 1] = temp;
            return;
        }

        int idx = 0;

        while (idx < node->entries.size() &&
               key > node->entries[idx].key) {
            idx++;
        }

        if (node->children[idx]->entries.size() == (2 * minDegree - 1)) {

            splitChild(node, idx);

            if (key > node->entries[idx].key) {
                idx++;
            }
        }

        insertNonFull(node->children[idx], key, value);
    }

public:

    explicit BTreeDB(int degree) {
        minDegree = degree;
        root = new Page(true);
    }

    void insert(const Key& key, const Data& value) {

        if (root->entries.size() == (2 * minDegree - 1)) {

            Page* newRoot = new Page(false);

            newRoot->children.push_back(root);

            splitChild(newRoot, 0);

            root = newRoot;
        }

        insertNonFull(root, key, value);
    }

    bool search(const Key& key, Data& out) {

        Page* current = root;

        while (current) {

            int i = 0;

            while (i < current->entries.size() &&
                   key > current->entries[i].key) {
                i++;
            }

            if (i < current->entries.size() &&
                current->entries[i].key == key) {

                out = current->entries[i].data;
                return true;
            }

            if (current->isLeaf) {
                return false;
            }

            current = current->children[i];
        }

        return false;
    }
};

int main() {

    BTreeDB<int, std::string> db(3);

    db.insert(15, "Apple");
    db.insert(7, "Orange");
    db.insert(30, "Banana");
    db.insert(22, "Mango");

    std::string result;

    if (db.search(22, result)) {
        std::cout << "Found: " << result << '\n';
    }
    else {
        std::cout << "Not found\n";
    }

    return 0;
}