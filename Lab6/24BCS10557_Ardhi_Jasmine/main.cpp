#include <iostream>
#include <vector>

using namespace std;

template <typename Key>
class BTreeNode
{
public:
    vector<Key> keys;
    vector<BTreeNode*> children;
    bool leaf;
    int t; // Minimum degree

    BTreeNode(int degree, bool isLeaf)
    {
        t = degree;
        leaf = isLeaf;
    }

    // Search function
    BTreeNode* search(Key key)
    {
        int i = 0;

        while (i < keys.size() && key > keys[i])
        {
            i++;
        }

        if (i < keys.size() && keys[i] == key)
        {
            return this;
        }

        if (leaf)
        {
            return nullptr;
        }

        return children[i]->search(key);
    }

    // Split child
    void splitChild(int i, BTreeNode* y)
    {
        BTreeNode* z = new BTreeNode(y->t, y->leaf);

        // Middle key index
        int mid = t - 1;

        // Copy last t-1 keys
        for (int j = 0; j < t - 1; j++)
        {
            z->keys.push_back(y->keys[j + t]);
        }

        // Copy children if not leaf
        if (!y->leaf)
        {
            for (int j = 0; j < t; j++)
            {
                z->children.push_back(y->children[j + t]);
            }
        }

        Key middleKey = y->keys[mid];

        // Resize old child
        y->keys.resize(mid);

        if (!y->leaf)
        {
            y->children.resize(t);
        }

        children.insert(children.begin() + i + 1, z);
        keys.insert(keys.begin() + i, middleKey);
    }

    // Insert into non-full node
    void insertNonFull(Key key)
    {
        int i = keys.size() - 1;

        if (leaf)
        {
            keys.push_back(key);

            while (i >= 0 && keys[i] > key)
            {
                keys[i + 1] = keys[i];
                i--;
            }

            keys[i + 1] = key;
        }
        else
        {
            while (i >= 0 && keys[i] > key)
            {
                i--;
            }

            i++;

            if (children[i]->keys.size() == 2 * t - 1)
            {
                splitChild(i, children[i]);

                if (key > keys[i])
                {
                    i++;
                }
            }

            children[i]->insertNonFull(key);
        }
    }
};

template <typename Key>
class BTree
{
private:
    BTreeNode<Key>* root;
    int t;

public:
    BTree(int degree)
    {
        root = nullptr;
        t = degree;
    }

    void insert(Key key)
    {
        if (root == nullptr)
        {
            root = new BTreeNode<Key>(t, true);
            root->keys.push_back(key);
            return;
        }

        if (root->keys.size() == 2 * t - 1)
        {
            BTreeNode<Key>* newRoot =
                new BTreeNode<Key>(t, false);

            newRoot->children.push_back(root);

            newRoot->splitChild(0, root);

            int i = 0;

            if (newRoot->keys[0] < key)
            {
                i++;
            }

            newRoot->children[i]->insertNonFull(key);

            root = newRoot;
        }
        else
        {
            root->insertNonFull(key);
        }
    }

    bool search(Key key)
    {
        if (root == nullptr)
        {
            return false;
        }

        return root->search(key) != nullptr;
    }

    void display(BTreeNode<Key>* node)
    {
        if (node == nullptr)
        {
            return;
        }

        for (auto key : node->keys)
        {
            cout << key << " ";
        }

        cout << endl;

        for (auto child : node->children)
        {
            display(child);
        }
    }

    void print()
    {
        display(root);
    }
};

int main()
{
    BTree<int> tree(3);

    tree.insert(10);
    tree.insert(20);
    tree.insert(5);
    tree.insert(6);
    tree.insert(12);
    tree.insert(30);
    tree.insert(7);
    tree.insert(17);

    cout << "B-Tree structure:\n";
    tree.print();

    cout << "\nSearch 12: ";

    if (tree.search(12))
    {
        cout << "Found";
    }
    else
    {
        cout << "Not Found";
    }

    return 0;
}