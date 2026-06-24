#include <iostream>
#include <vector>
#include <string>
using namespace std;

template <typename Key, typename Value>
class BTree
{
private:
    struct Entry
    {
        Key key;
        Value value;
    };

    struct Node
    {
        vector<Entry> entries;   // keys + values stored in this node
        vector<Node *> children; // child pointers 
        bool isLeaf = true;
    };

    Node *root;
    size_t minDeg; 
    size_t maxKeys; 
    size_t minKeys; 

    Entry *searchNode(Node *node, Key key)
    {
        int i = 0;
        while (i < (int)node->entries.size() && key > node->entries[i].key)
            i++;

        if (i < (int)node->entries.size() && key == node->entries[i].key)
            return &node->entries[i];

        if (node->isLeaf)
            return nullptr;

        return searchNode(node->children[i], key);
    }

    void splitChild(Node *parent, int idx)
    {
        Node *full = parent->children[idx];
        Node *right = new Node();
        right->isLeaf = full->isLeaf;

        int mid = (int)minDeg - 1; 

        for (int i = mid + 1; i < (int)full->entries.size(); i++)
            right->entries.push_back(full->entries[i]);

        if (!full->isLeaf)
        {
            for (int i = mid + 1; i < (int)full->children.size(); i++)
                right->children.push_back(full->children[i]);
            full->children.resize(mid + 1); 
        }

        Entry median = full->entries[mid];
        full->entries.resize(mid);

        parent->children.insert(parent->children.begin() + idx + 1, right);
        parent->entries.insert(parent->entries.begin() + idx, median);
    }

    void insertNonFull(Node *node, Key key, Value value)
    {
        int i = (int)node->entries.size() - 1;

        if (node->isLeaf)
        {\
            node->entries.push_back({}); 
            while (i >= 0 && key < node->entries[i].key)
            {
                node->entries[i + 1] = node->entries[i];
                i--;
            }
            node->entries[i + 1] = {key, value};
        }
        else
        {
            while (i >= 0 && key < node->entries[i].key)
                i--;
            i++; 

            if ((int)node->children[i]->entries.size() == (int)maxKeys)
            {
                splitChild(node, i);
                if (key > node->entries[i].key)
                    i++;
            }
            insertNonFull(node->children[i], key, value);
        }
    }
    void printNode(Node *node, int depth)
    {
        cout << "Depth " << depth << ": [ ";
        for (auto &e : node->entries)
            cout << e.key << " ";
        cout << "]\n";

        if (!node->isLeaf)
            for (auto *child : node->children)
                printNode(child, depth + 1);
    }

public:
    BTree(size_t t) : minDeg(t), maxKeys(2 * t - 1), minKeys(t - 1)
    {
        root = new Node();
    }
    Entry *search(Key key)
    {
        return searchNode(root, key);
    }

    void insert(Key key, Value value)
    {
        if (search(key))
            return;
        if ((int)root->entries.size() == (int)maxKeys)
        {
            Node *newRoot = new Node();
            newRoot->isLeaf = false;
            newRoot->children.push_back(root);
            splitChild(newRoot, 0);
            root = newRoot;
        }

        insertNonFull(root, key, value);
    }

    void print()
    {
        printNode(root, 0);
    }
};

int main()
{
    BTree<int, string> db(3); // min-degree = 3 => max 5 keys per node

    db.insert(10, "Tokyo");
    db.insert(20, "London");
    db.insert(5,  "Paris");
    db.insert(6,  "Berlin");
    db.insert(12, "Dubai");
    db.insert(30, "Sydney");
    db.insert(7,  "Mumbai");
    db.insert(17, "Toronto");

    cout << "=== Tree structure ===\n";
    db.print();

    cout << "\n=== Search ===\n";
    auto *r = db.search(12);
    if (r)
        cout << "Found: " << r->key << " -> " << r->value << "\n";
    else
        cout << "Not found\n";

    auto *r2 = db.search(99);
    if (r2)
        cout << "Found: " << r2->key << "\n";
    else
        cout << "Key 99 not found\n";

    return 0;
}