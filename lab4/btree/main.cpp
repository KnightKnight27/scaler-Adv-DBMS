#include <iostream>
#include <vector>
#include <optional>

template<typename Key, typename Row>
class DB {
private:
    struct Entry {
        Key key;
        Row row;

        Entry(Key k, Row r) : key(k), row(r) {}
    };

    struct BTree {
        bool leaf;
        std::vector<Entry> keys;
        std::vector<BTree*> children;

        BTree(bool isLeaf) {
            leaf = isLeaf;
        }
    };

    BTree* root;
    size_t t; // minimum degree

    bool isFull(BTree* node) {
        return node->keys.size() == 2 * t - 1;
    }

    std::optional<Row> searchRecursive(BTree* node, Key key) {
        int i = 0;

        while (i < node->keys.size() &&
               key > node->keys[i].key)
            i++;

        if (i < node->keys.size() &&
            node->keys[i].key == key)
            return node->keys[i].row;

        if (node->leaf)
            return std::nullopt;

        return searchRecursive(node->children[i], key);
    }

    void splitChild(BTree* parent, int i) {
        BTree* y = parent->children[i];
        BTree* z = new BTree(y->leaf);

        Entry middle = y->keys[t - 1];

        for (int j = t; j < y->keys.size(); j++)
            z->keys.push_back(y->keys[j]);

        if (!y->leaf) {
            for (int j = t; j < y->children.size(); j++)
                z->children.push_back(y->children[j]);
        }

        y->keys.resize(t - 1);

        if (!y->leaf)
            y->children.resize(t);

        parent->children.insert(
            parent->children.begin() + i + 1,
            z
        );

        parent->keys.insert(
            parent->keys.begin() + i,
            middle
        );
    }

    void insertNonFull(BTree* node,
                       Key key,
                       Row row) {
        int i = node->keys.size() - 1;

        if (node->leaf) {
            node->keys.emplace_back(key, row);

            while (i >= 0 &&
                   key < node->keys[i].key) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }

            node->keys[i + 1] = Entry(key, row);
        }
        else {
            while (i >= 0 &&
                   key < node->keys[i].key)
                i--;

            i++;

            if (isFull(node->children[i])) {
                splitChild(node, i);

                if (key > node->keys[i].key)
                    i++;
            }

            insertNonFull(node->children[i],
                          key,
                          row);
        }
    }

    void traverse(BTree* node) {
        int i;

        for (i = 0; i < node->keys.size(); i++) {
            if (!node->leaf)
                traverse(node->children[i]);

            std::cout << node->keys[i].key
                      << " ";
        }

        if (!node->leaf)
            traverse(node->children[i]);
    }

public:
    DB(size_t degree) {
        t = degree;
        root = new BTree(true);
    }

    std::optional<Row> Search(Key key) {
        return searchRecursive(root, key);
    }

    void Insert(Key key, Row row) {
        if (isFull(root)) {
            BTree* s = new BTree(false);

            s->children.push_back(root);

            splitChild(s, 0);

            int i = 0;

            if (key > s->keys[0].key)
                i++;

            insertNonFull(
                s->children[i],
                key,
                row
            );

            root = s;
        }
        else {
            insertNonFull(root, key, row);
        }
    }

    void Print() {
        traverse(root);
        std::cout << "\n";
    }
};

int main() {
    DB<int, std::string> db(3);

    db.Insert(10, "row10");
    db.Insert(20, "row20");
    db.Insert(5, "row5");
    db.Insert(6, "row6");
    db.Insert(12, "row12");
    db.Insert(30, "row30");
    db.Insert(7, "row7");
    db.Insert(17, "row17");

    db.Print();

    auto result = db.Search(12);

    if (result)
        std::cout << *result << "\n";
    else
        std::cout << "Not found\n";
}