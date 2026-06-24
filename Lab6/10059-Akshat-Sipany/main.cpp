#include <iostream>
#include <vector>
#include <string>

using namespace std;

template<typename TKey, typename TValue>
class BTreeIndex {
private:
    struct Record {
        TKey key;
        TValue value;
    };

    struct Node {
        vector<Record> records;
        vector<Node*> childNodes;
        bool isLeaf = true;
    };

    Node* rootNode;

    size_t lowerBound;
    size_t upperBound;
    size_t minRecords;
    size_t maxRecords;

private:
    Record* findInNode(Node* currentNode, TKey targetKey) {
        int idx = 0;

        while (idx < currentNode->records.size() && targetKey > currentNode->records[idx].key) {
            idx++;
        }

        if (idx < currentNode->records.size() && targetKey == currentNode->records[idx].key) {
            return &currentNode->records[idx];
        }

        if (currentNode->isLeaf) {
            return nullptr;
        }

        return findInNode(currentNode->childNodes[idx], targetKey);
    }

    void splitChildNode(Node* parentNode, int splitIndex) {
        Node* overflowNode = parentNode->childNodes[splitIndex];
        Node* promotedNode = new Node();
        promotedNode->isLeaf = overflowNode->isLeaf;

        int mid = lowerBound;

        Record pivotRecord = overflowNode->records[mid - 1];

        for (int i = mid; i < overflowNode->records.size(); i++) {
            promotedNode->records.push_back(overflowNode->records[i]);
        }

        overflowNode->records.resize(mid - 1);

        if (!overflowNode->isLeaf) {
            for (int i = mid; i < overflowNode->childNodes.size(); i++) {
                promotedNode->childNodes.push_back(overflowNode->childNodes[i]);
            }

            overflowNode->childNodes.resize(mid);
        }

        parentNode->childNodes.insert(
            parentNode->childNodes.begin() + splitIndex + 1,
            promotedNode
        );

        parentNode->records.insert(
            parentNode->records.begin() + splitIndex,
            pivotRecord
        );
    }

    void insertIntoNode(Node* currentNode, TKey newKey, TValue newValue) {
        int pos = currentNode->records.size() - 1;

        if (currentNode->isLeaf) {
            Record newRecord{newKey, newValue};
            currentNode->records.push_back(newRecord);

            while (pos >= 0 && newKey < currentNode->records[pos].key) {
                currentNode->records[pos + 1] = currentNode->records[pos];
                pos--;
            }

            currentNode->records[pos + 1] = newRecord;
        } else {
            while (pos >= 0 && newKey < currentNode->records[pos].key) {
                pos--;
            }

            pos++;

            if (currentNode->childNodes[pos]->records.size() == maxRecords) {
                splitChildNode(currentNode, pos);

                if (newKey > currentNode->records[pos].key) {
                    pos++;
                }
            }

            insertIntoNode(currentNode->childNodes[pos], newKey, newValue);
        }
    }

    void printNode(Node* currentNode, int depth) {
        cout << "Depth " << depth << ": ";

        for (auto& rec : currentNode->records) {
            cout << rec.key << " ";
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
        rootNode = new Node();

        lowerBound = degree;
        upperBound = 2 * degree;

        minRecords = lowerBound - 1;
        maxRecords = upperBound - 1;
    }

    Record* search(TKey targetKey) {
        return findInNode(rootNode, targetKey);
    }

    void insert(TKey newKey, TValue newValue) {
        if (search(newKey) != nullptr) {
            return;
        }

        if (rootNode->records.size() == maxRecords) {
            Node* newRoot = new Node();
            newRoot->isLeaf = false;
            newRoot->childNodes.push_back(rootNode);

            splitChildNode(newRoot, 0);
            rootNode = newRoot;
        }

        insertIntoNode(rootNode, newKey, newValue);
    }

    void print() {
        printNode(rootNode, 0);
    }
};

int main() {
    BTreeIndex<int, string> index(3);

    index.insert(10, "Alpha");
    index.insert(20, "Beta");
    index.insert(5, "Gamma");
    index.insert(6, "Delta");
    index.insert(12, "Epsilon");
    index.insert(30, "Zeta");
    index.insert(7, "Eta");
    index.insert(17, "Theta");

    index.print();

    auto* result = index.search(12);

    if (result != nullptr) {
        cout << "\nFound: " << result->key << " -> " << result->value << endl;
    } else {
        cout << "\nKey not found" << endl;
    }

    return 0;
}
