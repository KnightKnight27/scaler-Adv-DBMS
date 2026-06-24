#include <iostream>
#include <vector>

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

    Entry searchRecursive(BTree* node, Key key) {
        size_t i = 0;

        while (i < node->keys.size() &&
               key > node->keys[i].key) {
            i++;
        }

        if (i < node->keys.size() &&
            key == node->keys[i].key) {
            return node->keys[i];
        }

        if (node->leaf) {
            return Entry{Key(), Row()};
        }

        return searchRecursive(node->children[i], key);
    }

    void splitChild(BTree* parent,
                    size_t index,
                    BTree* child) {

        BTree* newNode = new BTree(child->leaf);

        size_t t = minDegree;

        Entry middle = child->keys[t - 1];

        for (size_t i = t; i < child->keys.size(); i++) {
            newNode->keys.push_back(child->keys[i]);
        }

        if (!child->leaf) {
            for (size_t i = t; i < child->children.size(); i++) {
                newNode->children.push_back(child->children[i]);
            }
        }

        child->keys.resize(t - 1);

        if (!child->leaf) {
            child->children.resize(t);
        }

        parent->children.insert(
            parent->children.begin() + index + 1,
            newNode);

        parent->keys.insert(
            parent->keys.begin() + index,
            middle);
    }

    void insertNonFull(BTree* node,
                       Key key,
                       Row row) {

        int i = static_cast<int>(node->keys.size()) - 1;

        if (node->leaf) {

            node->keys.push_back({key, row});

            while (i >= 0 &&
                   node->keys[i].key > key) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }

            node->keys[i + 1] = {key, row};
        }
        else {

            while (i >= 0 &&
                   node->keys[i].key > key) {
                i--;
            }

            i++;

            if (node->children[i]->keys.size()
                == (2 * minDegree - 1)) {

                splitChild(node,
                           i,
                           node->children[i]);

                if (key > node->keys[i].key) {
                    i++;
                }
            }

            insertNonFull(
                node->children[i],
                key,
                row);
        }
    }

public:
    DB(size_t degree) {
        minDegree = degree;
        root = new BTree(true);
    }

    Entry Search(Key key) {
        return searchRecursive(root, key);
    }

    void Insert(Key key, Row row) {

        if (root->keys.size()
            == (2 * minDegree - 1)) {

            BTree* newRoot =
                new BTree(false);

            newRoot->children.push_back(root);

            splitChild(newRoot, 0, root);

            root = newRoot;
        }

        insertNonFull(root, key, row);
    }
};

int main() {

    DB<int, std::string> db(3);

    db.Insert(10, "Row10");
    db.Insert(20, "Row20");
    db.Insert(5, "Row5");
    db.Insert(30, "Row30");
    db.Insert(40, "Row40");

    auto result = db.Search(20);

    std::cout << result.row << std::endl;

    return 0;
}