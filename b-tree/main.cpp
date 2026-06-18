#include <iostream>
#include <vector>
#include <string>
using namespace std;

template <typename KeyType, typename DataType>
class BalancedTree
{
private:
    struct Record
    {
        KeyType id;
        DataType payload;
    };

    struct TreeNode
    {
        vector<Record> records;     // keys + values stored in this node
        vector<TreeNode *> branches; // child pointers
        bool terminal = true;
    };

    TreeNode *rootNode;
    size_t minDegree;
    size_t maxRecords;
    size_t minRecords;

    Record *lookupInNode(TreeNode *node, KeyType id)
    {
        int i = 0;
        while (i < (int)node->records.size() && id > node->records[i].id)
            i++;

        if (i < (int)node->records.size() && id == node->records[i].id)
            return &node->records[i];

        if (node->terminal)
            return nullptr;

        return lookupInNode(node->branches[i], id);
    }

    void divideBranch(TreeNode *parent, int idx)
    {
        TreeNode *full = parent->branches[idx];
        TreeNode *right = new TreeNode();
        right->terminal = full->terminal;

        int mid = (int)minDegree - 1;

        for (int i = mid + 1; i < (int)full->records.size(); i++)
            right->records.push_back(full->records[i]);

        if (!full->terminal)
        {
            for (int i = mid + 1; i < (int)full->branches.size(); i++)
                right->branches.push_back(full->branches[i]);
            full->branches.resize(mid + 1);
        }

        Record median = full->records[mid];
        full->records.resize(mid);

        parent->branches.insert(parent->branches.begin() + idx + 1, right);
        parent->records.insert(parent->records.begin() + idx, median);
    }

    void addToNonFull(TreeNode *node, KeyType id, DataType payload)
    {
        int i = (int)node->records.size() - 1;

        if (node->terminal)
        {
            node->records.push_back({});
            while (i >= 0 && id < node->records[i].id)
            {
                node->records[i + 1] = node->records[i];
                i--;
            }
            node->records[i + 1] = {id, payload};
        }
        else
        {
            while (i >= 0 && id < node->records[i].id)
                i--;
            i++;

            if ((int)node->branches[i]->records.size() == (int)maxRecords)
            {
                divideBranch(node, i);
                if (id > node->records[i].id)
                    i++;
            }
            addToNonFull(node->branches[i], id, payload);
        }
    }
    void displayNode(TreeNode *node, int depth)
    {
        cout << "Depth " << depth << ": [ ";
        for (auto &r : node->records)
            cout << r.id << " ";
        cout << "]\n";

        if (!node->terminal)
            for (auto *branch : node->branches)
                displayNode(branch, depth + 1);
    }

public:
    BalancedTree(size_t t) : minDegree(t), maxRecords(2 * t - 1), minRecords(t - 1)
    {
        rootNode = new TreeNode();
    }
    Record *find(KeyType id)
    {
        return lookupInNode(rootNode, id);
    }

    void add(KeyType id, DataType payload)
    {
        if (find(id))
            return;
        if ((int)rootNode->records.size() == (int)maxRecords)
        {
            TreeNode *newRoot = new TreeNode();
            newRoot->terminal = false;
            newRoot->branches.push_back(rootNode);
            divideBranch(newRoot, 0);
            rootNode = newRoot;
        }

        addToNonFull(rootNode, id, payload);
    }

    void display()
    {
        displayNode(rootNode, 0);
    }
};

int main()
{
    BalancedTree<int, string> store(3); // min-degree = 3 => max 5 keys per node

    store.add(10, "Tokyo");
    store.add(20, "London");
    store.add(5,  "Paris");
    store.add(6,  "Berlin");
    store.add(12, "Dubai");
    store.add(30, "Sydney");
    store.add(7,  "Mumbai");
    store.add(17, "Toronto");

    cout << "=== Tree structure ===\n";
    store.display();

    cout << "\n=== Search ===\n";
    auto *result = store.find(12);
    if (result)
        cout << "Found: " << result->id << " -> " << result->payload << "\n";
    else
        cout << "Not found\n";

    auto *missing = store.find(99);
    if (missing)
        cout << "Found: " << missing->id << "\n";
    else
        cout << "Key 99 not found\n";

    return 0;
}
