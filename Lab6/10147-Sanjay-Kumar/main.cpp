#include <iostream>
#include <vector>
#include <string>

using namespace std;

template <typename K, typename V>
class BTree {
private:
    struct Record {
        K key;
        V value;
    };

    struct Node {
        vector<Record> records;
        vector<Node*> children;
        bool isLeaf = true;
    };

    Node* root;

    size_t degree;
    size_t maxChildren;
    size_t minKeys;
    size_t maxKeys;

private:
    Record* findKey(Node* node, K key) {
        int idx = 0;

        while (idx < node->records.size() &&
               key > node->records[idx].key) {
            idx++;
        }

        if (idx < node->records.size() &&
            key == node->records[idx].key) {
            return &node->records[idx];
        }

        if (node->isLeaf) {
            return nullptr;
        }

        return findKey(node->children[idx], key);
    }

    void splitChild(Node* parent, int childIndex) {
        Node* leftChild = parent->children[childIndex];
        Node* rightChild = new Node();

        rightChild->isLeaf = leftChild->isLeaf;

        int mid = degree;

        Record promotedRecord = leftChild->records[mid - 1];

        for (int i = mid; i < leftChild->records.size(); i++) {
            rightChild->records.push_back(leftChild->records[i]);
        }

        leftChild->records.resize(mid - 1);

        if (!leftChild->isLeaf) {
            for (int i = mid; i < leftChild->children.size(); i++) {
                rightChild->children.push_back(leftChild->children[i]);
            }

            leftChild->children.resize(mid);
        }

        parent->children.insert(
            parent->children.begin() + childIndex + 1,
            rightChild
        );

        parent->records.insert(
            parent->records.begin() + childIndex,
            promotedRecord
        );
    }

    void insertNode(Node* node, K key, V value) {
        int pos = node->records.size() - 1;

        if (node->isLeaf) {
            Record newRecord{key, value};

            node->records.push_back(newRecord);

            while (pos >= 0 &&
                   key < node->records[pos].key) {
                node->records[pos + 1] = node->records[pos];
                pos--;
            }

            node->records[pos + 1] = newRecord;
        }
        else {
            while (pos >= 0 &&
                   key < node->records[pos].key) {
                pos--;
            }

            pos++;

            if (node->children[pos]->records.size() == maxKeys) {
                splitChild(node, pos);

                if (key > node->records[pos].key) {
                    pos++;
                }
            }

            insertNode(node->children[pos], key, value);
        }
    }

    void printNode(Node* node, int level) {
        cout << "Level " << level << ": ";

        for (auto& rec : node->records) {
            cout << rec.key << " ";
        }

        cout << endl;

        if (!node->isLeaf) {
            for (auto* child : node->children) {
                printNode(child, level + 1);
            }
        }
    }

public:
    BTree(size_t t) {
        root = new Node();

        degree = t;
        maxChildren = 2 * t;

        minKeys = degree - 1;
        maxKeys = maxChildren - 1;
    }

    Record* search(K key) {
        return findKey(root, key);
    }

    void insert(K key, V value) {
        if (search(key) != nullptr) {
            return;
        }

        if (root->records.size() == maxKeys) {
            Node* newRoot = new Node();

            newRoot->isLeaf = false;
            newRoot->children.push_back(root);

            splitChild(newRoot, 0);

            root = newRoot;
        }

        insertNode(root, key, value);
    }

    void print() {
        printNode(root, 0);
    }
};

int main() {
    BTree<int, string> bt(3);

    bt.insert(1001, "Rahul");
    bt.insert(1002, "Priya");
    bt.insert(1003, "Arjun");
    bt.insert(1004, "Sneha");
    bt.insert(1005, "Karan");
    bt.insert(1006, "Ananya");
    bt.insert(1007, "Rohit");
    bt.insert(1008, "Meera");

    bt.print();

    auto* result = bt.search(1005);

    if (result != nullptr) {
        cout << "\nFound: "
             << result->key
             << " -> "
             << result->value
             << endl;
    } else {
        cout << "\nKey not found" << endl;
    }

    return 0;
}
