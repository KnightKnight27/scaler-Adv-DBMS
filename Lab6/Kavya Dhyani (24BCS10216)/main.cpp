#include <iostream>
#include <vector>

template<typename Key, typename Row>
class DB {
public:
    struct Entry {
        Key key;
        Row row;
    };

private:
    struct Btree {
        bool isLeaf;
        std::vector<Entry> entries;
        std::vector<Btree*> children;
        Btree(bool leaf) : isLeaf(leaf) {}
    };

    size_t minDegree;
    Btree *root;

    void SplitChild(Btree* parent, size_t idx, Btree* child) {
        Btree* newChild = new Btree(child->isLeaf);
        size_t t = minDegree;

        for (size_t j = 0; j < t - 1; j++) {
            newChild->entries.push_back(child->entries[t + j]);
        }
        if (!child->isLeaf) {
            for (size_t j = 0; j < t; j++) {
                newChild->children.push_back(child->children[t + j]);
            }
        }

        Entry median = child->entries[t - 1];
        child->entries.resize(t - 1);
        if (!child->isLeaf) {
            child->children.resize(t);
        }

        parent->children.insert(parent->children.begin() + idx + 1, newChild);
        parent->entries.insert(parent->entries.begin() + idx, median);
    }

    void InsertNonFull(Btree* node, Key key, Row row) {
        int i = node->entries.size() - 1;

        if (node->isLeaf) {
            node->entries.push_back({key, row});
            while (i >= 0 && node->entries[i].key > key) {
                node->entries[i + 1] = node->entries[i];
                i--;
            }
            node->entries[i + 1] = {key, row};
        } else {
            while (i >= 0 && node->entries[i].key > key) {
                i--;
            }
            i++;

            if (node->children[i]->entries.size() == 2 * minDegree - 1) {
                SplitChild(node, i, node->children[i]);
                if (node->entries[i].key < key) {
                    i++;
                }
            }
            InsertNonFull(node->children[i], key, row);
        }
    }

public:
    DB(size_t degree) {
        minDegree = degree;
        root = new Btree(true);
    }

    Entry Search(Key key) {
        Btree* current = root;
        while (current != nullptr) {
            size_t i = 0;
            while (i < current->entries.size() && key > current->entries[i].key) {
                i++;
            }

            if (i < current->entries.size() && key == current->entries[i].key) {
                return current->entries[i];
            }

            if (current->isLeaf) {
                break;
            }

            current = current->children[i];
        }
        return Entry{Key(), Row()};
    }

    void Insert(Key key, Row row) {
        Btree* r = root;
        if (r->entries.size() == 2 * minDegree - 1) {
            Btree* s = new Btree(false);
            root = s;
            s->children.push_back(r);
            SplitChild(s, 0, r);
            InsertNonFull(s, key, row);
        } else {
            InsertNonFull(r, key, row);
        }
    }
};

int main() {
    DB<int, std::string> db(3);
    db.Insert(10, "A");
    db.Insert(20, "B");
    db.Insert(5, "C");

    auto res = db.Search(20);
    std::cout << res.row << "\n";
    return 0;
}