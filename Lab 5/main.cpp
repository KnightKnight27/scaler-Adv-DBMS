#include <iostream>
#include <vector>

using namespace std;

class Node
{
public:
    int value;
    bool isRed;

    Node *left;
    Node *right;
    Node *parent;

    Node(int val)
    {
        value = val;
        isRed = true;

        left = nullptr;
        right = nullptr;
        parent = nullptr;
    }
};

class RedBlackTree
{
private:
    Node *root;

    void leftRotate(Node *node)
    {
        Node *child = node->right;

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

    void rightRotate(Node *node)
    {
        Node *child = node->left;

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
        else if (node == node->parent->left)
        {
            node->parent->left = child;
        }
        else
        {
            node->parent->right = child;
        }

        child->right = node;
        node->parent = child;
    }

    void balanceTree(Node *node)
    {
        while (node != root && node->parent->isRed)
        {
            Node *parent = node->parent;
            Node *grandParent = parent->parent;

            if (grandParent == nullptr)
            {
                break;
            }

            if (parent == grandParent->left)
            {
                Node *uncle = grandParent->right;

                if (uncle != nullptr && uncle->isRed)
                {
                    parent->isRed = false;
                    uncle->isRed = false;
                    grandParent->isRed = true;

                    node = grandParent;
                }
                else
                {
                    if (node == parent->right)
                    {
                        node = parent;
                        leftRotate(node);

                        parent = node->parent;
                    }

                    rightRotate(grandParent);

                    parent->isRed = false;
                    grandParent->isRed = true;
                }
            }
            else
            {
                Node *uncle = grandParent->left;

                if (uncle != nullptr && uncle->isRed)
                {
                    parent->isRed = false;
                    uncle->isRed = false;
                    grandParent->isRed = true;

                    node = grandParent;
                }
                else
                {
                    if (node == parent->left)
                    {
                        node = parent;
                        rightRotate(node);

                        parent = node->parent;
                    }

                    leftRotate(grandParent);

                    parent->isRed = false;
                    grandParent->isRed = true;
                }
            }
        }

        root->isRed = false;
    }

    void inorderTraversal(Node *node)
    {
        if (node == nullptr)
        {
            return;
        }

        inorderTraversal(node->left);

        cout << node->value;

        if (node->isRed)
        {
            cout << "(R) ";
        }
        else
        {
            cout << "(B) ";
        }

        inorderTraversal(node->right);
    }

public:
    RedBlackTree()
    {
        root = nullptr;
    }

    void insert(int val)
    {
        Node *newNode = new Node(val);

        Node *current = root;
        Node *parent = nullptr;

        while (current != nullptr)
        {
            parent = current;

            if (val < current->value)
            {
                current = current->left;
            }
            else if (val > current->value)
            {
                current = current->right;
            }
            else
            {
                delete newNode;
                return;
            }
        }

        newNode->parent = parent;

        if (parent == nullptr)
        {
            root = newNode;
        }
        else if (val < parent->value)
        {
            parent->left = newNode;
        }
        else
        {
            parent->right = newNode;
        }

        balanceTree(newNode);
    }

    bool search(int val)
    {
        Node *current = root;

        while (current != nullptr)
        {
            if (current->value == val)
            {
                return true;
            }

            if (val < current->value)
            {
                current = current->left;
            }
            else
            {
                current = current->right;
            }
        }

        return false;
    }

    void display()
    {
        inorderTraversal(root);
        cout << endl;
    }
};

int main()
{
    RedBlackTree tree;

    vector<int> data = {10, 20, 30, 15, 5, 25};

    cout << "Inserting elements:\n";

    for (int num : data)
    {
        cout << "Insert -> " << num << endl;
        tree.insert(num);
    }

    cout << "\nTree contents:\n";
    tree.display();

    cout << "\nSearch Results:\n";

    cout << "15 : ";
    cout << (tree.search(15) ? "Found" : "Not Found") << endl;

    cout << "99 : ";
    cout << (tree.search(99) ? "Found" : "Not Found") << endl;

    return 0;
}