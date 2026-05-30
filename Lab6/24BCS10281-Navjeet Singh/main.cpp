#include <iostream>
#include <vector>

template <typename K, typename V>
class DB {
public:
    struct Record {
        K key;
        V value;
    };

private:
    struct Node {
        bool leaf;
        std::vector<Record> records;
        std::vector<Node*> childPtrs;

        explicit Node(bool isLeafNode) : leaf(isLeafNode) {}
    };

    size_t degree;
    Node* rootNode;

    void splitNode(Node* parent, size_t childIndex, Node* child) {
        Node* rightNode = new Node(child->leaf);
        size_t t = degree;

        for (size_t i = 0; i < t - 1; ++i) {
            rightNode->records.push_back(child->records[t + i]);
        }

        if (!child->leaf) {
            for (size_t i = 0; i < t; ++i) {
                rightNode->childPtrs.push_back(child->childPtrs[t + i]);
            }
        }

        Record promoted = child->records[t - 1];

        child->records.resize(t - 1);

        if (!child->leaf) {
            child->childPtrs.resize(t);
        }

        parent->childPtrs.insert(
            parent->childPtrs.begin() + childIndex + 1,
            rightNode
        );

        parent->records.insert(
            parent->records.begin() + childIndex,
            promoted
        );
    }

    void insertIntoNonFull(Node* current, const K& key, const V& value) {
        int pos = static_cast<int>(current->records.size()) - 1;

        if (current->leaf) {
            current->records.push_back({key, value});

            while (pos >= 0 && current->records[pos].key > key) {
                current->records[pos + 1] = current->records[pos];
                --pos;
            }

            current->records[pos + 1] = {key, value};
        } else {
            while (pos >= 0 && current->records[pos].key > key) {
                --pos;
            }

            ++pos;

            if (current->childPtrs[pos]->records.size() ==
                (2 * degree - 1)) {

                splitNode(current, pos, current->childPtrs[pos]);

                if (key > current->records[pos].key) {
                    ++pos;
                }
            }

            insertIntoNonFull(current->childPtrs[pos], key, value);
        }
    }

public:
    explicit DB(size_t minDegree)
        : degree(minDegree), rootNode(new Node(true)) {}

    Record Search(const K& key) {
        Node* node = rootNode;

        while (node != nullptr) {
            size_t idx = 0;

            while (idx < node->records.size() &&
                   key > node->records[idx].key) {
                ++idx;
            }

            if (idx < node->records.size() &&
                node->records[idx].key == key) {
                return node->records[idx];
            }

            if (node->leaf) {
                break;
            }

            node = node->childPtrs[idx];
        }

        return Record{K(), V()};
    }

    void Insert(const K& key, const V& value) {
        Node* currentRoot = rootNode;

        if (currentRoot->records.size() ==
            (2 * degree - 1)) {

            Node* newRoot = new Node(false);
            rootNode = newRoot;

            newRoot->childPtrs.push_back(currentRoot);

            splitNode(newRoot, 0, currentRoot);
            insertIntoNonFull(newRoot, key, value);
        } else {
            insertIntoNonFull(currentRoot, key, value);
        }
    }
};

int main() {
    DB<int, std::string> database(3);

    database.Insert(7, "Canada");
    database.Insert(19, "Russia");
    database.Insert(21, "America");

    auto result1 = database.Search(7);
    auto result2 = database.Search(19);
    auto result3 = database.Search(21);

    std::cout << result1.value << std::endl;
    std::cout << result2.value << std::endl;
    std::cout << result3.value << std::endl;

    return 0;
}