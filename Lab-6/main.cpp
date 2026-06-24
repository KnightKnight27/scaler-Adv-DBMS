#include <iostream>
#include <vector>

using namespace std;

template <typename T>
class BTreeNode
{
public:
    vector<T> keys;
    vector<BTreeNode*> children;
    bool leaf;
    int t;

    BTreeNode(int degree, bool isLeaf)
    {
        t = degree;
        leaf = isLeaf;
    }

    BTreeNode* search(T key)
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

    void splitChild(int index, BTreeNode* child)
    {
        BTreeNode* newNode = new BTreeNode(child->t, child->leaf);

        for (int j = 0; j < t - 1; j++)
        {
            newNode->keys.push_back(child->keys[j + t]);
        }

        if (!child->leaf)
        {
            for (int j = 0; j < t; j++)
            {
                newNode->children.push_back(child->children[j + t]);
            }
        }

        T middleKey = child->keys[t - 1];

        child->keys.resize(t - 1);

        if (!child->leaf)
        {
            child->children.resize(t);
        }

        children.insert(children.begin() + index + 1, newNode);
        keys.insert(keys.begin() + index, middleKey);
    }

    void insertNonFull(T key)
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

    void display()
    {
        cout << "[";

        for (int i = 0; i < keys.size(); i++)
        {
            cout << keys[i];

            if (i != keys.size() - 1)
            {
                cout << " ";
            }
        }

        cout << "]" << endl;

        for (auto child : children)
        {
            child->display();
        }
    }
};

template <typename T>
class BTree
{
private:
    BTreeNode<T>* root;
    int t;

public:
    BTree(int degree)
    {
        root = nullptr;
        t = degree;
    }

    bool search(T key)
    {
        if (root == nullptr)
        {
            return false;
        }

        return root->search(key) != nullptr;
    }

    void insert(T key)
    {
        if (search(key))
        {
            cout << "Duplicate key ignored." << endl;
            return;
        }

        if (root == nullptr)
        {
            root = new BTreeNode<T>(t, true);
            root->keys.push_back(key);
            return;
        }

        if (root->keys.size() == 2 * t - 1)
        {
            BTreeNode<T>* newRoot = new BTreeNode<T>(t, false);

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

    void print()
    {
        if (root == nullptr)
        {
            cout << "Tree is empty" << endl;
            return;
        }

        root->display();
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

    cout << "B-Tree Structure:" << endl;
    tree.print();

    cout << endl;

    cout << "Search 12: ";
    cout << (tree.search(12) ? "Found" : "Not Found") << endl;

    cout << "Search 25: ";
    cout << (tree.search(25) ? "Found" : "Not Found") << endl;

    return 0;
}