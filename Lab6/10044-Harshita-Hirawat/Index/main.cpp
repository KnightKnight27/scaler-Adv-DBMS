#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>

using namespace std;

template<typename Key, typename Value>

class BTree {
private:
    struct Entry {
        Key key;
        Value value;

        Entry(Key k, Value v) : key(k), value(v) {}
    };

    struct Node {
        bool leaf;
        vector<Entry> entries;
        vector<Node*> children;

        Node(bool isLeaf = true) : leaf(isLeaf) {}
    };

    Node* root;
    int t; // minimum degree

private:
    Entry* search(Node* node, const Key& key) {
        int i = 0;

        while(i < (int)node->entries.size() && key > node->entries[i].key) i++;

        if(i < (int)node->entries.size() && key == node->entries[i].key) return &node->entries[i];
        if(node->leaf) return nullptr;

        return search(node->children[i], key);
    }

    void splitChild(Node* parent, int index) {
        Node* leftChild = parent->children[index];
        Node* rightChild = new Node(leftChild->leaf);

        Entry promoted = leftChild->entries[t - 1];

        // Move right-half entries
        for(int i=t; i < (int)leftChild->entries.size(); i++)
            rightChild->entries.push_back(leftChild->entries[i]);

        // Move right-half children if internal node
        if(!leftChild->leaf) {
            for(int i=t; i < (int)leftChild->children.size(); i++)
                rightChild->children.push_back(leftChild->children[i]);

            leftChild->children.resize(t);
        }

        // Keep left half
        leftChild->entries.resize(t-1);

        // Insert promoted entry into parent
        parent->entries.insert(
            parent->entries.begin() + index,
            promoted
        );

        // Insert new child into parent
        parent->children.insert(
            parent->children.begin() + index + 1,
            rightChild
        );
    }

    void insertNonFull(Node* node, const Key& key, const Value& value) {
        int i = (int)node->entries.size() - 1;

        if(node->leaf) {
            node->entries.push_back(
                Entry(key, value)
            );

            while(i >= 0 && key < node->entries[i].key) {
                node->entries[i + 1] = node->entries[i];
                i--;
            }

            node->entries[i + 1] = Entry(key, value);
        }
        else {
            while(i >= 0 && key < node->entries[i].key) i--;
            i++;

            // Split full child before descending
            if(node->children[i]->entries.size() == (size_t)(2 * t - 1)) {
                splitChild(node, i);
                if(key > node->entries[i].key) i++;
            }

            insertNonFull(node->children[i], key, value);
        }
    }

    void print(Node* node, int level) {
        cout << "Level " << level << " : ";
        for(auto& entry : node->entries) cout << entry.key << " ";
        cout << "\n";

        if(!node->leaf) {
            for(auto child : node->children)
                print(child, level + 1);
        }
    }

    void destroy(Node* node) {
        if(!node) return;
        for(auto child : node->children) destroy(child);
        delete node;
    }

public:
    BTree(int degree) {
        if(degree < 2) throw invalid_argument("Minimum degree must be at least 2");

        t = degree;
        root = new Node(true);
    }

    ~BTree() {
        destroy(root);
    }

    bool insert(const Key& key, const Value& value) {
        // Prevent duplicate keys
        if(search(root, key)) return false;

        // Root full
        if(root->entries.size() == (size_t)(2 * t - 1)) {
            Node* newRoot = new Node(false);
            newRoot->children.push_back(root);
            splitChild(newRoot, 0);
            root = newRoot;
        }

        insertNonFull(root, key, value);

        return true;
    }

    Value* find(const Key& key) {
        Entry* result = search(root, key);

        if(!result) return nullptr;

        return &result->value;
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
    bt.insert(3, "I");
    bt.insert(4, "J");
    bt.insert(2, "K");
    bt.insert(40, "L");
    bt.insert(50, "M");

    cout << "B-Tree Structure:\n\n";
    bt.printTree();

    auto value = bt.find(17);

    if(value)
        cout << "\nFound: " << *value << "\n";
    else
        cout << "\nNot Found\n";

    return 0;
}