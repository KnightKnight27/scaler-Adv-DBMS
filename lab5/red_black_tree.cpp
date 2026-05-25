#include <iostream>
#include <limits>
#include <queue>
#include <string>
#include <vector>

enum class NodeColor
{
    Red,
    Black
};

struct RBNode
{
    int data;
    NodeColor color;
    RBNode* parent;
    RBNode* left;
    RBNode* right;

    explicit RBNode(int value)
        : data(value),
          color(NodeColor::Red),
          parent(nullptr),
          left(nullptr),
          right(nullptr)
    {
    }
};

class RBTree
{
public:
    RBTree() = default;

    ~RBTree()
    {
        clear(root);
    }

    void insert(int value)
    {
        RBNode* newNode = new RBNode(value);
        RBNode* parent = nullptr;
        RBNode* current = root;

        while (current != nullptr)
        {
            parent = current;

            if (value == current->data)
            {
                delete newNode;
                return;
            }

            current = value < current->data ? current->left : current->right;
        }

        newNode->parent = parent;

        if (parent == nullptr)
        {
            root = newNode;
        }
        else if (value < parent->data)
        {
            parent->left = newNode;
        }
        else
        {
            parent->right = newNode;
        }

        repairAfterInsert(newNode);
    }

    bool search(int value) const
    {
        RBNode* current = root;

        while (current != nullptr)
        {
            if (value == current->data)
            {
                return true;
            }

            current = value < current->data ? current->left : current->right;
        }

        return false;
    }

    void showInorder() const
    {
        std::cout << "Inorder: ";
        showInorder(root);
        std::cout << '\n';
    }

    void showLevelOrder() const
    {
        if (root == nullptr)
        {
            std::cout << "Level order: empty\n";
            return;
        }

        std::queue<RBNode*> pending;
        pending.push(root);

        std::cout << "Level order: ";

        while (!pending.empty())
        {
            RBNode* node = pending.front();
            pending.pop();

            std::cout << node->data << colorText(node) << ' ';

            if (node->left != nullptr)
            {
                pending.push(node->left);
            }

            if (node->right != nullptr)
            {
                pending.push(node->right);
            }
        }

        std::cout << '\n';
    }

    bool validate() const
    {
        if (root == nullptr)
        {
            return true;
        }

        if (root->color != NodeColor::Black)
        {
            return false;
        }

        int requiredBlackHeight = -1;
        return validateNode(
            root,
            std::numeric_limits<int>::min(),
            std::numeric_limits<int>::max(),
            0,
            requiredBlackHeight
        );
    }

private:
    RBNode* root{nullptr};

    static NodeColor getColor(RBNode* node)
    {
        return node == nullptr ? NodeColor::Black : node->color;
    }

    static std::string colorText(RBNode* node)
    {
        return getColor(node) == NodeColor::Red ? "(R)" : "(B)";
    }

    void rotateLeft(RBNode* node)
    {
        RBNode* pivot = node->right;
        node->right = pivot->left;

        if (pivot->left != nullptr)
        {
            pivot->left->parent = node;
        }

        pivot->parent = node->parent;

        if (node->parent == nullptr)
        {
            root = pivot;
        }
        else if (node == node->parent->left)
        {
            node->parent->left = pivot;
        }
        else
        {
            node->parent->right = pivot;
        }

        pivot->left = node;
        node->parent = pivot;
    }

    void rotateRight(RBNode* node)
    {
        RBNode* pivot = node->left;
        node->left = pivot->right;

        if (pivot->right != nullptr)
        {
            pivot->right->parent = node;
        }

        pivot->parent = node->parent;

        if (node->parent == nullptr)
        {
            root = pivot;
        }
        else if (node == node->parent->right)
        {
            node->parent->right = pivot;
        }
        else
        {
            node->parent->left = pivot;
        }

        pivot->right = node;
        node->parent = pivot;
    }

    void repairAfterInsert(RBNode* node)
    {
        while (node != root && getColor(node->parent) == NodeColor::Red)
        {
            RBNode* parent = node->parent;
            RBNode* grandparent = parent->parent;

            if (parent == grandparent->left)
            {
                RBNode* uncle = grandparent->right;

                if (getColor(uncle) == NodeColor::Red)
                {
                    parent->color = NodeColor::Black;
                    uncle->color = NodeColor::Black;
                    grandparent->color = NodeColor::Red;
                    node = grandparent;
                }
                else
                {
                    if (node == parent->right)
                    {
                        node = parent;
                        rotateLeft(node);
                        parent = node->parent;
                        grandparent = parent->parent;
                    }

                    parent->color = NodeColor::Black;
                    grandparent->color = NodeColor::Red;
                    rotateRight(grandparent);
                }
            }
            else
            {
                RBNode* uncle = grandparent->left;

                if (getColor(uncle) == NodeColor::Red)
                {
                    parent->color = NodeColor::Black;
                    uncle->color = NodeColor::Black;
                    grandparent->color = NodeColor::Red;
                    node = grandparent;
                }
                else
                {
                    if (node == parent->left)
                    {
                        node = parent;
                        rotateRight(node);
                        parent = node->parent;
                        grandparent = parent->parent;
                    }

                    parent->color = NodeColor::Black;
                    grandparent->color = NodeColor::Red;
                    rotateLeft(grandparent);
                }
            }
        }

        root->color = NodeColor::Black;
    }

    void showInorder(RBNode* node) const
    {
        if (node == nullptr)
        {
            return;
        }

        showInorder(node->left);
        std::cout << node->data << colorText(node) << ' ';
        showInorder(node->right);
    }

    bool validateNode(
        RBNode* node,
        int minValue,
        int maxValue,
        int blackCount,
        int& requiredBlackHeight
    ) const
    {
        if (node == nullptr)
        {
            if (requiredBlackHeight == -1)
            {
                requiredBlackHeight = blackCount;
                return true;
            }

            return blackCount == requiredBlackHeight;
        }

        if (node->data <= minValue || node->data >= maxValue)
        {
            return false;
        }

        if (node->color == NodeColor::Black)
        {
            ++blackCount;
        }

        if (node->color == NodeColor::Red)
        {
            if (getColor(node->left) == NodeColor::Red || getColor(node->right) == NodeColor::Red)
            {
                return false;
            }
        }

        return validateNode(node->left, minValue, node->data, blackCount, requiredBlackHeight)
            && validateNode(node->right, node->data, maxValue, blackCount, requiredBlackHeight);
    }

    void clear(RBNode* node)
    {
        if (node == nullptr)
        {
            return;
        }

        clear(node->left);
        clear(node->right);
        delete node;
    }
};

int main()
{
    RBTree tree;
    const std::vector<int> inputValues{
        45, 20, 60, 10, 30, 50, 70, 5, 15, 25, 35, 65, 80
    };

    for (int value : inputValues)
    {
        tree.insert(value);
        std::cout << "Inserted " << value
                  << " | tree valid: " << (tree.validate() ? "yes" : "no")
                  << '\n';
    }

    std::cout << '\n';
    tree.showInorder();
    tree.showLevelOrder();

    std::cout << "\nSearch 35: " << (tree.search(35) ? "found" : "not found") << '\n';
    std::cout << "Search 99: " << (tree.search(99) ? "found" : "not found") << '\n';
    std::cout << "Validation result: " << (tree.validate() ? "passed" : "failed") << '\n';

    return 0;
}
