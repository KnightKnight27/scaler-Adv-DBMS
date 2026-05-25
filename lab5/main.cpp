#include <iostream>
using namespace std;

enum Color
{
    RED,
    BLACK
};

struct Node
{
    int data;
    Color color;
    Node *left;
    Node *right;
    Node *parent;

    Node(int value)
    {
        data = value;
        color = RED;
        left = right = parent = nullptr;
    }
};

class RedBlackTree
{
private:
    Node *root;

    void leftRotate(Node *x)
    {
        Node *y = x->right;
        x->right = y->left;

        if (y->left != nullptr)
            y->left->parent = x;

        y->parent = x->parent;

        if (x->parent == nullptr)
            root = y;
        else if (x == x->parent->left)
            x->parent->left = y;
        else
            x->parent->right = y;

        y->left = x;
        x->parent = y;
    }

    void rightRotate(Node *y)
    {
        Node *x = y->left;
        y->left = x->right;

        if (x->right != nullptr)
            x->right->parent = y;

        x->parent = y->parent;

        if (y->parent == nullptr)
            root = x;
        else if (y == y->parent->left)
            y->parent->left = x;
        else
            y->parent->right = x;

        x->right = y;
        y->parent = x;
    }

    void fixInsert(Node *k)
    {
        while (k != root && k->parent->color == RED)
        {
            Node *parent = k->parent;
            Node *grandparent = parent->parent;

            if (parent == grandparent->left)
            {
                Node *uncle = grandparent->right;

                if (uncle != nullptr && uncle->color == RED)
                {
                    parent->color = BLACK;
                    uncle->color = BLACK;
                    grandparent->color = RED;
                    k = grandparent;
                }
                else
                {
                    if (k == parent->right)
                    {
                        k = parent;
                        leftRotate(k);
                    }

                    parent->color = BLACK;
                    grandparent->color = RED;
                    rightRotate(grandparent);
                }
            }
            else
            {
                Node *uncle = grandparent->left;

                if (uncle != nullptr && uncle->color == RED)
                {
                    parent->color = BLACK;
                    uncle->color = BLACK;
                    grandparent->color = RED;
                    k = grandparent;
                }
                else
                {
                    if (k == parent->left)
                    {
                        k = parent;
                        rightRotate(k);
                    }

                    parent->color = BLACK;
                    grandparent->color = RED;
                    leftRotate(grandparent);
                }
            }
        }

        root->color = BLACK;
    }

    void inorderHelper(Node *node)
    {
        if (node == nullptr)
            return;

        inorderHelper(node->left);

        cout << node->data << " (";

        if (node->color == RED)
            cout << "RED";
        else
            cout << "BLACK";

        cout << ") ";

        inorderHelper(node->right);
    }

public:
    RedBlackTree()
    {
        root = nullptr;
    }

    void insert(int key)
    {
        Node *node = new Node(key);
        Node *y = nullptr;
        Node *x = root;

        while (x != nullptr)
        {
            y = x;

            if (node->data < x->data)
                x = x->left;
            else
                x = x->right;
        }

        node->parent = y;

        if (y == nullptr)
            root = node;
        else if (node->data < y->data)
            y->left = node;
        else
            y->right = node;

        if (node->parent == nullptr)
        {
            node->color = BLACK;
            return;
        }

        if (node->parent->parent == nullptr)
            return;

        fixInsert(node);
    }

    void inorder()
    {
        inorderHelper(root);
        cout << endl;
    }
};

int main()
{
    RedBlackTree tree;

    tree.insert(10);
    tree.insert(20);
    tree.insert(30);
    tree.insert(15);
    tree.insert(25);
    tree.insert(5);
    tree.insert(1);

    cout << "Inorder Traversal of Red-Black Tree:" << endl;
    tree.inorder();

    return 0;
}
