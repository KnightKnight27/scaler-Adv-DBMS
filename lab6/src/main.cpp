#include <iostream>
#include <string>
#include <vector>

using namespace std;

// A small B-Tree based database index.
// Key = indexed column value
// Row = row address / row data / record pointer
template <typename Key, typename Row>
class DB
{
private:
    struct Entry
    {
        Key key;
        Row row;
    };

    struct BTreeNode
    {
        bool leaf;
        vector<Entry> entries;
        vector<BTreeNode*> children;

        BTreeNode(bool isLeaf)
        {
            leaf = isLeaf;
        }
    };

    BTreeNode* root;
    int minDegree; // minimum children in a non-root internal node

public:
    DB(int degree)
    {
        minDegree = degree;
        root = new BTreeNode(true);
    }

    Row* Search(Key key)
    {
        return SearchRecursive(root, key);
    }

    void Insert(Key key, Row row)
    {
        Entry entry{key, row};

        if(isFull(root))
        {
            BTreeNode* newRoot = new BTreeNode(false);
            newRoot->children.push_back(root);

            splitChild(newRoot, 0);
            root = newRoot;
        }

        insertNonFull(root, entry);
    }

    void Display()
    {
        cout << "B-Tree keys in sorted order:\n";
        displayInOrder(root);
        cout << "\n";
    }

private:
    int maxKeys()
    {
        return 2 * minDegree - 1;
    }

    bool isFull(BTreeNode* node)
    {
        return node->entries.size() == maxKeys();
    }

    Row* SearchRecursive(BTreeNode* node, Key key)
    {
        int i = 0;

        while(i < node->entries.size() && key > node->entries[i].key)
        {
            i++;
        }

        if(i < node->entries.size() && key == node->entries[i].key)
        {
            return &node->entries[i].row;
        }

        if(node->leaf)
        {
            return nullptr;
        }

        return SearchRecursive(node->children[i], key);
    }

    void insertNonFull(BTreeNode* node, Entry entry)
    {
        int i = static_cast<int>(node->entries.size()) - 1;

        if(node->leaf)
        {
            node->entries.push_back(entry);

            while(i >= 0 && entry.key < node->entries[i].key)
            {
                node->entries[i + 1] = node->entries[i];
                i--;
            }

            node->entries[i + 1] = entry;
            return;
        }

        while(i >= 0 && entry.key < node->entries[i].key)
        {
            i--;
        }
        i++;

        if(isFull(node->children[i]))
        {
            splitChild(node, i);

            if(entry.key > node->entries[i].key)
            {
                i++;
            }
        }

        insertNonFull(node->children[i], entry);
    }

    void splitChild(BTreeNode* parent, int childIndex)
    {
        BTreeNode* fullChild = parent->children[childIndex];
        BTreeNode* rightChild = new BTreeNode(fullChild->leaf);

        Entry middleEntry = fullChild->entries[minDegree - 1];

        for(int j = 0; j < minDegree - 1; j++)
        {
            rightChild->entries.push_back(fullChild->entries[j + minDegree]);
        }

        if(!fullChild->leaf)
        {
            for(int j = 0; j < minDegree; j++)
            {
                rightChild->children.push_back(fullChild->children[j + minDegree]);
            }
            fullChild->children.resize(minDegree);
        }

        fullChild->entries.resize(minDegree - 1);

        parent->children.insert(parent->children.begin() + childIndex + 1, rightChild);
        parent->entries.insert(parent->entries.begin() + childIndex, middleEntry);
    }

    void displayInOrder(BTreeNode* node)
    {
        int i;

        for(i = 0; i < node->entries.size(); i++)
        {
            if(!node->leaf)
            {
                displayInOrder(node->children[i]);
            }

            cout << node->entries[i].key << " -> " << node->entries[i].row << "\n";
        }

        if(!node->leaf)
        {
            displayInOrder(node->children[i]);
        }
    }
};

int main()
{
    DB<int, string> studentIndex(3);

    studentIndex.Insert(10, "ROW_OF_10");
    studentIndex.Insert(20, "ROW_OF_20");
    studentIndex.Insert(5, "ROW_OF_5");
    studentIndex.Insert(6, "ROW_OF_6");
    studentIndex.Insert(12, "ROW_OF_12");
    studentIndex.Insert(30, "ROW_OF_30");
    studentIndex.Insert(7, "ROW_OF_7");
    studentIndex.Insert(17, "ROW_OF_17");

    studentIndex.Display();

    int key = 12;
    string* row = studentIndex.Search(key);

    if(row != nullptr)
    {
        cout << "\nFound key " << key << " at " << *row << "\n";
    }
    else
    {
        cout << "\nKey not found\n";
    }

    return 0;
}
