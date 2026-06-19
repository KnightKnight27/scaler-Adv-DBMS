#include <iostream>
#include <limits>
#include <queue>
#include <string>
#include <vector>

enum class Color
{
    Red,
    Black
};

struct Node
{
    int key;
    Color color;
    Node* parent;
    Node* left;
    Node* right;

    explicit Node(int value)
        : key(value), color(Color::Red), parent(nullptr), left(nullptr), right(nullptr)
    {
    }
};

class RedBlackTree
{
public:
    RedBlackTree() = default;

    ~RedBlackTree()
    {
        destroy(root);
    }

    void insert(int key)
    {
        Node* parent = nullptr;
        Node* current = root;

        while (current != nullptr)
        {
            parent = current;

            if (key == current->key)
            {
                return;
            }

            current = (key < current->key) ? current->left : current->right;
        }

        Node* node = new Node(key);
        node->parent = parent;

        if (parent == nullptr)
        {
            root = node;
        }
        else if (key < parent->key)
        {
            parent->left = node;
        }
        else
        {
            parent->right = node;
        }

        fixAfterInsert(node);
    }

    bool contains(int key) const
    {
        Node* current = root;

        while (current != nullptr)
        {
            if (key == current->key)
            {
                return true;
            }

            current = (key < current->key) ? current->left : current->right;
        }

        return false;
    }

    void printInOrder() const
    {
        std::cout << "Inorder traversal: ";
        printInOrder(root);
        std::cout << '\n';
    }

    void printLevelOrder() const
    {
        if (root == nullptr)
        {
            std::cout << "Tree is empty\n";
            return;
        }

        std::queue<Node*> nodes;
        nodes.push(root);

        std::cout << "Level order: ";

        while (!nodes.empty())
        {
            Node* current = nodes.front();
            nodes.pop();

            std::cout << current->key << colorLabel(current) << ' ';

            if (current->left != nullptr)
            {
                nodes.push(current->left);
            }

            if (current->right != nullptr)
            {
                nodes.push(current->right);
            }
        }

        std::cout << '\n';
    }

    bool isValid() const
    {
        if (root == nullptr)
        {
            return true;
        }

        if (root->color != Color::Black)
        {
            return false;
        }

        int blackHeight = -1;
        return validate(
            root,
            std::numeric_limits<int>::min(),
            std::numeric_limits<int>::max(),
            0,
            blackHeight
        );
    }

private:
    Node* root{nullptr};

    static Color colorOf(Node* node)
    {
        return node == nullptr ? Color::Black : node->color;
    }

    static std::string colorLabel(Node* node)
    {
        return colorOf(node) == Color::Red ? "(R)" : "(B)";
    }

    void rotateLeft(Node* node)
    {
        Node* child = node->right;
        node->right = child->left;

        if (child->left != nullptr)
        {
            child->left->parent = node;
        }

        child->parent = node->parent;

        if (node->parent == nullptr)
        {
            root = child;
        }
        else if (node == node->parent->left)
        {
            node->parent->left = child;
        }
        else
        {
            node->parent->right = child;
        }

        child->left = node;
        node->parent = child;
    }

    void rotateRight(Node* node)
    {
        Node* child = node->left;
        node->left = child->right;

        if (child->right != nullptr)
        {
            child->right->parent = node;
        }

        child->parent = node->parent;

        if (node->parent == nullptr)
        {
            root = child;
        }
        else if (node == node->parent->right)
        {
            node->parent->right = child;
        }
        else
        {
            node->parent->left = child;
        }

        child->right = node;
        node->parent = child;
    }

    void fixAfterInsert(Node* node)
    {
        while (node != root && colorOf(node->parent) == Color::Red)
        {
            Node* parent = node->parent;
            Node* grandparent = parent->parent;

            if (parent == grandparent->left)
            {
                Node* uncle = grandparent->right;

                if (colorOf(uncle) == Color::Red)
                {
                    parent->color = Color::Black;
                    uncle->color = Color::Black;
                    grandparent->color = Color::Red;
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

                    parent->color = Color::Black;
                    grandparent->color = Color::Red;
                    rotateRight(grandparent);
                }
            }
            else
            {
                Node* uncle = grandparent->left;

                if (colorOf(uncle) == Color::Red)
                {
                    parent->color = Color::Black;
                    uncle->color = Color::Black;
                    grandparent->color = Color::Red;
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

                    parent->color = Color::Black;
                    grandparent->color = Color::Red;
                    rotateLeft(grandparent);
                }
            }
        }

        root->color = Color::Black;
    }

    void printInOrder(Node* node) const
    {
        if (node == nullptr)
        {
            return;
        }

        printInOrder(node->left);
        std::cout << node->key << colorLabel(node) << ' ';
        printInOrder(node->right);
    }

    bool validate(Node* node, int low, int high, int blackCount, int& expectedBlackHeight) const
    {
        if (node == nullptr)
        {
            if (expectedBlackHeight == -1)
            {
                expectedBlackHeight = blackCount;
                return true;
            }

            return blackCount == expectedBlackHeight;
        }

        if (node->key <= low || node->key >= high)
        {
            return false;
        }

        if (node->color == Color::Black)
        {
            ++blackCount;
        }

        if (node->color == Color::Red)
        {
            if (colorOf(node->left) == Color::Red || colorOf(node->right) == Color::Red)
            {
                return false;
            }
        }

        return validate(node->left, low, node->key, blackCount, expectedBlackHeight)
            && validate(node->right, node->key, high, blackCount, expectedBlackHeight);
    }

    void destroy(Node* node)
    {
        if (node == nullptr)
        {
            return;
        }

        destroy(node->left);
        destroy(node->right);
        delete node;
    }
};

int main()
{
    RedBlackTree tree;
    const std::vector<int> values{
        41, 38, 31, 12, 19, 8, 25, 50, 60, 55, 5, 1, 70
    };

    for (int value : values)
    {
        tree.insert(value);
        std::cout << "Inserted " << value
                  << " -> valid: " << (tree.isValid() ? "yes" : "no")
                  << '\n';
    }

    std::cout << '\n';
    tree.printInOrder();
    tree.printLevelOrder();

    std::cout << "\nSearch 25: " << (tree.contains(25) ? "found" : "not found") << '\n';
    std::cout << "Search 99: " << (tree.contains(99) ? "found" : "not found") << '\n';
    std::cout << "Final validation: " << (tree.isValid() ? "passed" : "failed") << '\n';

    return 0;
}
