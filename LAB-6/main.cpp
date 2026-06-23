#include <iostream>
#include <vector>
#include <string>

using namespace std;

template<typename TKey, typename TValue>
class BTreeIndex {
private:
    struct Entry {
        TKey key;
        TValue value;
    };

    struct Node {
        vector<Entry> entries;
        vector<Node*> childNodes;
        bool isLeaf = true;
    };

    Node* root;

    size_t minDegree;
    size_t maxDegree;
    size_t minKeys;
    size_t maxKeys;

private:
    Entry* findInNode(Node* currentNode, TKey targetKey) {
        int index = 0;

        while (index < currentNode->entries.size() && targetKey > currentNode->entries[index].key) {
            index++;
        }

        if (index < currentNode->entries.size() && targetKey == currentNode->entries[index].key) {
            return &currentNode->entries[index];
        }

        if (currentNode->isLeaf) {
            return nullptr;
        }

        return findInNode(currentNode->childNodes[index], targetKey);
    }

    void splitChildNode(Node* parentNode, int splitIndex) {
        Node* child = parentNode->childNodes[splitIndex];
        Node* rightNode = new Node();
        rightNode->isLeaf = child->isLeaf;

        int median = minDegree;

        Entry pivot = child->entries[median - 1];

        for (int j = median; j < child->entries.size(); j++) {
            rightNode->entries.push_back(child->entries[j]);
        }

        child->entries.resize(median - 1);

        if (!child->isLeaf) {
            for (int j = median; j < child->childNodes.size(); j++) {
                rightNode->childNodes.push_back(child->childNodes[j]);
            }

            child->childNodes.resize(median);
        }

        parentNode->childNodes.insert(
            parentNode->childNodes.begin() + splitIndex + 1,
            rightNode
        );

        parentNode->entries.insert(
            parentNode->entries.begin() + splitIndex,
            pivot
        );
    }

    void insertIntoNode(Node* currentNode, TKey newKey, TValue newValue) {
        int posIndex = currentNode->entries.size() - 1;

        if (currentNode->isLeaf) {
            Entry newEntry{newKey, newValue};
            currentNode->entries.push_back(newEntry);

            while (posIndex >= 0 && newKey < currentNode->entries[posIndex].key) {
                currentNode->entries[posIndex + 1] = currentNode->entries[posIndex];
                posIndex--;
            }

            currentNode->entries[posIndex + 1] = newEntry;
        } else {
            while (posIndex >= 0 && newKey < currentNode->entries[posIndex].key) {
                posIndex--;
            }

            posIndex++;

            if (currentNode->childNodes[posIndex]->entries.size() == maxKeys) {
                splitChildNode(currentNode, posIndex);

                if (newKey > currentNode->entries[posIndex].key) {
                    posIndex++;
                }
            }

            insertIntoNode(currentNode->childNodes[posIndex], newKey, newValue);
        }
    }

    void printNode(Node* currentNode, int depth) {
        cout << "Depth " << depth << ": ";

        for (auto& ent : currentNode->entries) {
            cout << ent.key << " ";
        }

        cout << endl;

        if (!currentNode->isLeaf) {
            for (auto* child : currentNode->childNodes) {
                printNode(child, depth + 1);
            }
        }
    }

public:
    BTreeIndex(size_t degree) {
        root = new Node();

        minDegree = degree;
        maxDegree = 2 * degree;

        minKeys = minDegree - 1;
        maxKeys = maxDegree - 1;
    }

    Entry* search(TKey targetKey) {
        return findInNode(root, targetKey);
    }

    void insert(TKey newKey, TValue newValue) {
        if (search(newKey) != nullptr) {
            return;
        }

        if (root->entries.size() == maxKeys) {
            Node* newRoot = new Node();
            newRoot->isLeaf = false;
            newRoot->childNodes.push_back(root);

            splitChildNode(newRoot, 0);
            root = newRoot;
        }

        insertIntoNode(root, newKey, newValue);
    }

    void print() {
        printNode(root, 0);
    }
};

int main() {

    BTreeIndex<int, string> index(3);

    index.insert(10, "One");
    index.insert(20, "Two");
    index.insert(5, "Three");
    index.insert(6, "Four");
    index.insert(12, "Five");
    index.insert(30, "Six");
    index.insert(7, "Seven");
    index.insert(17, "Eight");

    index.print();

    auto* result = index.search(12);

    if (result != nullptr) {
        cout << "\nFound- " << result->key << " -> " << result->value << endl;
    } else {
        cout << "\nKey not found " << endl;
    }

    return 0;
}
