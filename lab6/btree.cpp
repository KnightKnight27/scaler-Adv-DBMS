#include <algorithm>
#include <iostream>
#include <memory>
#include <queue>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

class BTree
{
public:
    explicit BTree(int minimumDegree)
        : degree(minimumDegree), root(std::make_unique<Node>(true))
    {
        if (degree < 2)
        {
            throw std::invalid_argument("Minimum degree must be at least 2");
        }
    }

    void insert(int key)
    {
        if (contains(key))
        {
            return;
        }

        if (isFull(root.get()))
        {
            auto newRoot = std::make_unique<Node>(false);
            newRoot->children.push_back(std::move(root));
            splitChild(newRoot.get(), 0);
            root = std::move(newRoot);
        }

        insertNonFull(root.get(), key);
    }

    bool contains(int key) const
    {
        return search(root.get(), key);
    }

    std::vector<int> inorder() const
    {
        std::vector<int> output;
        collectInorder(root.get(), output);
        return output;
    }

    void printInorder() const
    {
        std::cout << "Sorted keys: ";
        const std::vector<int> keys = inorder();

        for (int key : keys)
        {
            std::cout << key << ' ';
        }

        std::cout << '\n';
    }

    void printLevels() const
    {
        std::queue<const Node*> pending;
        pending.push(root.get());

        int level = 0;
        std::cout << "Level order layout:\n";

        while (!pending.empty())
        {
            const int count = static_cast<int>(pending.size());
            std::cout << "Level " << level << ": ";

            for (int i = 0; i < count; ++i)
            {
                const Node* node = pending.front();
                pending.pop();

                std::cout << formatNode(node) << ' ';

                for (const auto& child : node->children)
                {
                    pending.push(child.get());
                }
            }

            std::cout << '\n';
            ++level;
        }
    }

    bool validate() const
    {
        const std::vector<int> keys = inorder();
        if (!std::is_sorted(keys.begin(), keys.end()))
        {
            return false;
        }

        int leafDepth = -1;
        return validateNode(root.get(), true, 0, leafDepth, nullptr, nullptr);
    }

private:
    struct Node
    {
        explicit Node(bool leafNode)
            : leaf(leafNode)
        {
        }

        bool leaf;
        std::vector<int> keys;
        std::vector<std::unique_ptr<Node>> children;
    };

    int degree;
    std::unique_ptr<Node> root;

    bool isFull(const Node* node) const
    {
        return static_cast<int>(node->keys.size()) == (2 * degree - 1);
    }

    bool search(const Node* node, int key) const
    {
        auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);

        if (it != node->keys.end() && *it == key)
        {
            return true;
        }

        if (node->leaf)
        {
            return false;
        }

        const int childIndex = static_cast<int>(it - node->keys.begin());
        return search(node->children[childIndex].get(), key);
    }

    void splitChild(Node* parent, int childIndex)
    {
        Node* child = parent->children[childIndex].get();
        auto sibling = std::make_unique<Node>(child->leaf);

        const int median = child->keys[degree - 1];

        sibling->keys.assign(child->keys.begin() + degree, child->keys.end());
        child->keys.resize(degree - 1);

        if (!child->leaf)
        {
            std::move(
                child->children.begin() + degree,
                child->children.end(),
                std::back_inserter(sibling->children)
            );
            child->children.resize(degree);
        }

        parent->keys.insert(parent->keys.begin() + childIndex, median);
        parent->children.insert(
            parent->children.begin() + childIndex + 1,
            std::move(sibling)
        );
    }

    void insertNonFull(Node* node, int key)
    {
        if (node->leaf)
        {
            auto position = std::lower_bound(node->keys.begin(), node->keys.end(), key);
            node->keys.insert(position, key);
            return;
        }

        int childIndex = static_cast<int>(
            std::upper_bound(node->keys.begin(), node->keys.end(), key) - node->keys.begin()
        );

        if (isFull(node->children[childIndex].get()))
        {
            splitChild(node, childIndex);

            if (key > node->keys[childIndex])
            {
                ++childIndex;
            }
        }

        insertNonFull(node->children[childIndex].get(), key);
    }

    void collectInorder(const Node* node, std::vector<int>& output) const
    {
        for (std::size_t i = 0; i < node->keys.size(); ++i)
        {
            if (!node->leaf)
            {
                collectInorder(node->children[i].get(), output);
            }

            output.push_back(node->keys[i]);
        }

        if (!node->leaf)
        {
            collectInorder(node->children.back().get(), output);
        }
    }

    bool validateNode(
        const Node* node,
        bool isRoot,
        int depth,
        int& leafDepth,
        const int* lower,
        const int* upper
    ) const
    {
        if (!std::is_sorted(node->keys.begin(), node->keys.end()))
        {
            return false;
        }

        const int keyCount = static_cast<int>(node->keys.size());
        if (!isRoot && (keyCount < degree - 1 || keyCount > 2 * degree - 1))
        {
            return false;
        }

        if (isRoot && keyCount > 2 * degree - 1)
        {
            return false;
        }

        for (int key : node->keys)
        {
            if ((lower != nullptr && key <= *lower) || (upper != nullptr && key >= *upper))
            {
                return false;
            }
        }

        if (node->leaf)
        {
            if (leafDepth == -1)
            {
                leafDepth = depth;
            }

            return leafDepth == depth;
        }

        if (static_cast<int>(node->children.size()) != keyCount + 1)
        {
            return false;
        }

        for (int i = 0; i <= keyCount; ++i)
        {
            const int* childLower = lower;
            const int* childUpper = upper;

            if (i > 0)
            {
                childLower = &node->keys[i - 1];
            }

            if (i < keyCount)
            {
                childUpper = &node->keys[i];
            }

            if (!validateNode(node->children[i].get(), false, depth + 1, leafDepth, childLower, childUpper))
            {
                return false;
            }
        }

        return true;
    }

    static std::string formatNode(const Node* node)
    {
        std::ostringstream stream;
        stream << '[';

        for (std::size_t i = 0; i < node->keys.size(); ++i)
        {
            if (i > 0)
            {
                stream << '|';
            }

            stream << node->keys[i];
        }

        stream << ']';
        return stream.str();
    }
};

int main()
{
    BTree tree(3);
    const std::vector<int> values{
        42, 18, 7, 29, 63, 54, 11, 3, 91, 75, 33, 48,
        57, 60, 2, 5, 8, 14, 21, 24, 27, 36, 39, 69
    };

    for (int value : values)
    {
        tree.insert(value);
        std::cout << "Inserted " << value
                  << " | valid: " << (tree.validate() ? "yes" : "no")
                  << '\n';
    }

    std::cout << '\n';
    tree.printInorder();
    tree.printLevels();

    std::cout << "\nSearch 54: " << (tree.contains(54) ? "found" : "not found") << '\n';
    std::cout << "Search 100: " << (tree.contains(100) ? "found" : "not found") << '\n';
    std::cout << "Final validation: " << (tree.validate() ? "passed" : "failed") << '\n';

    return 0;
}
