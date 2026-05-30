#include <iostream>
#include <vector>
#include <string>

template <typename K, typename V>
class DataMap
{
public:
    struct Item
    {
        K key;
        V value;
    };

private:
    struct TreeNode
    {
        bool leafNode;
        std::vector<Item> records;
        std::vector<TreeNode *> branches;
        TreeNode(bool isLeaf) : leafNode(isLeaf) {}
    };

    size_t degreeFactor;
    TreeNode *treeRoot;

    void divideNode(TreeNode *parent, size_t position, TreeNode *fullChild)
    {
        TreeNode *separateNode = new TreeNode(fullChild->leafNode);
        size_t mid = degreeFactor;

        for (size_t j = 0; j < mid - 1; j++)
        {
            separateNode->records.push_back(fullChild->records[mid + j]);
        }

        if (!fullChild->leafNode)
        {
            for (size_t j = 0; j < mid; j++)
            {
                separateNode->branches.push_back(fullChild->branches[mid + j]);
            }
        }

        Item middleItem = fullChild->records[mid - 1];

        fullChild->records.resize(mid - 1);
        if (!fullChild->leafNode)
        {
            fullChild->branches.resize(mid);
        }

        parent->branches.insert(parent->branches.begin() + position + 1, separateNode);
        parent->records.insert(parent->records.begin() + position, middleItem);
    }

    void pushSafe(TreeNode *node, K key, V value)
    {
        int index = node->records.size() - 1;

        if (node->leafNode)
        {
            node->records.push_back({key, value});
            while (index >= 0 && node->records[index].key > key)
            {
                node->records[index + 1] = node->records[index];
                index--;
            }
            node->records[index + 1] = {key, value};
        }
        else
        {
            while (index >= 0 && node->records[index].key > key)
            {
                index--;
            }
            index++;

            if (node->branches[index]->records.size() == 2 * degreeFactor - 1)
            {
                divideNode(node, index, node->branches[index]);
                if (node->records[index].key < key)
                {
                    index++;
                }
            }
            pushSafe(node->branches[index], key, value);
        }
    }

public:
    DataMap(size_t degree)
    {
        degreeFactor = degree;
        treeRoot = new TreeNode(true);
    }

    Item findKey(K key)
    {
        TreeNode *activeNode = treeRoot;
        while (activeNode != nullptr)
        {
            size_t i = 0;
            while (i < activeNode->records.size() && key > activeNode->records[i].key)
            {
                i++;
            }

            if (i < activeNode->records.size() && key == activeNode->records[i].key)
            {
                return activeNode->records[i];
            }

            if (activeNode->leafNode)
            {
                break;
            }

            activeNode = activeNode->branches[i];
        }
        return Item{K(), V()};
    }

    void save(K key, V value)
    {
        TreeNode *currentRoot = treeRoot;

        if (currentRoot->records.size() == 2 * degreeFactor - 1)
        {
            TreeNode *newRoot = new TreeNode(false);
            treeRoot = newRoot;
            newRoot->branches.push_back(currentRoot);
            divideNode(newRoot, 0, currentRoot);
            pushSafe(newRoot, key, value);
        }
        else
        {
            pushSafe(currentRoot, key, value);
        }
    }
};

int main()
{
    DataMap<int, std::string> mapper(3);
    mapper.save(10, "A");
    mapper.save(20, "B");
    mapper.save(5, "C");

    auto result = mapper.findKey(20);
    std::cout << result.value << "\n";
    return 0;
}