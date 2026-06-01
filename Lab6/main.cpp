#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>

using namespace std;

template<typename Key, typename Value>
class BTree {
private:
    struct Record {
        Key key;
        Value value;
    };

    struct Node {
        bool leaf;
        vector<Record> records;
        vector<Node*> children;

        Node(bool isLeaf = true) {
            leaf = isLeaf;
        }
    };

    Node* root;
    int t; // minimum degree

private:
    Record* search(Node* node, Key key) {
        int i = 0;

        while (i < (int)node->records.size() && key > node->records[i].key) i++;

        if (i < (int)node->records.size() && key == node->records[i].key) return &node->records[i];

        if (node->leaf) return nullptr;

        return search(node->children[i], key);
    }

    void splitChild(Node* parent, int index) {
        Node* fullChild = parent->children[index];
        Node* newNode = new Node(fullChild->leaf);

        Record middleRecord = fullChild->records[t - 1];

        for (int i = t; i < (int)fullChild->records.size(); i++) newNode->records.push_back(fullChild->records[i]);

        fullChild->records.resize(t - 1);

        if (!fullChild->leaf) {
            for (int i = t; i < (int)fullChild->children.size(); i++) newNode->children.push_back(fullChild->children[i]);

            fullChild->children.resize(t);
        }

        parent->children.insert(parent->children.begin() + index + 1, newNode);
        parent->records.insert(parent->records.begin() + index, middleRecord);
    }

    void insertNonFull(Node* node, Key key, Value value) {
        int i = (int)node->records.size() - 1;

        if (node->leaf) {
            Record rec{key, value};
            node->records.push_back(rec);

            while (i >= 0 && key < node->records[i].key) {
                node->records[i + 1] = node->records[i];
                i--;
            }

            node->records[i + 1] = rec;
        } else {
            while (i >= 0 && key < node->records[i].key)i--;

            i++;

            if (node->children[i]->records.size() == (size_t)(2 * t - 1)) {
                splitChild(node, i);

                if (key > node->records[i].key) i++;
            }

            insertNonFull(node->children[i], key, value);
        }
    }

    void print(Node* node, int level) {
        cout << "Level " << level << " : ";

        for (auto& r : node->records)cout << r.key << " ";

        cout << "\n";

        if (!node->leaf) {
            for (auto child : node->children) print(child, level + 1);
        }
    }

public:
    BTree(int degree) {
        if (degree < 2) throw invalid_argument("Degree must be >= 2");

        t = degree;
        root = new Node(true);
    }

    bool insert(Key key, Value value) {
        if (search(root, key))
            return false;

        if (root->records.size() == (size_t)(2 * t - 1)) {
            Node* newRoot = new Node(false);
            newRoot->children.push_back(root);

            splitChild(newRoot, 0);
            root = newRoot;
        }

        insertNonFull(root, key, value);
        return true;
    }

    Value* find(Key key) {
        Record* rec = search(root, key);
        if (!rec) return nullptr;
        return &rec->value;
    }

    void printTree() {
        print(root, 0);
    }
};

int main() {
    BTree<int, string> bt(3);

    bt.insert(10, "A");
    bt.insert(20, "B");
    bt.insert(5, "C");
    bt.insert(6, "D");
    bt.insert(12, "E");
    bt.insert(30, "F");
    bt.insert(7, "G");
    bt.insert(17, "H");

    bt.printTree();

    auto val = bt.find(12);
    if (val) cout << "\nFound : " << *val << endl;

    return 0;
}