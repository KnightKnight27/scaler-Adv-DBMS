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

    Node(int val)
    {
        data = val;
        color = RED;

        left = right = parent = NULL;
    }
};

class RBTree
{

private:
    Node *root;

    // LEFT ROTATION
    void leftRotate(Node *x)
    {

        Node *y = x->right;

        x->right = y->left;

        if (y->left != NULL)
        {
            y->left->parent = x;
        }

        y->parent = x->parent;

        if (x->parent == NULL)
        {
            root = y;
        }
        else if (x == x->parent->left)
        {
            x->parent->left = y;
        }
        else
        {
            x->parent->right = y;
        }

        y->left = x;
        x->parent = y;
    }

    // RIGHT ROTATION
    void rightRotate(Node *y)
    {

        Node *x = y->left;

        y->left = x->right;

        if (x->right != NULL)
        {
            x->right->parent = y;
        }

        x->parent = y->parent;

        if (y->parent == NULL)
        {
            root = x;
        }
        else if (y == y->parent->left)
        {
            y->parent->left = x;
        }
        else
        {
            y->parent->right = x;
        }

        x->right = y;
        y->parent = x;
    }

    // FIX INSERTION
    void fixInsert(Node *node)
    {

        while (node != root &&
               node->parent->color == RED)
        {

            Node *parent = node->parent;
            Node *grandparent = parent->parent;

            // PARENT IS LEFT CHILD
            if (parent == grandparent->left)
            {

                Node *uncle = grandparent->right;

                // CASE 1: UNCLE IS RED
                if (uncle != NULL &&
                    uncle->color == RED)
                {

                    parent->color = BLACK;
                    uncle->color = BLACK;
                    grandparent->color = RED;

                    node = grandparent;
                }
                else
                {

                    // CASE 2: LR
                    if (node == parent->right)
                    {

                        leftRotate(parent);

                        node = parent;
                        parent = node->parent;
                    }

                    // CASE 3: LL
                    rightRotate(grandparent);

                    Color temp = parent->color;
                    parent->color = grandparent->color;
                    grandparent->color = temp;
                }
            }

            // PARENT IS RIGHT CHILD
            else
            {

                Node *uncle = grandparent->left;

                // CASE 1: UNCLE IS RED
                if (uncle != NULL &&
                    uncle->color == RED)
                {

                    parent->color = BLACK;
                    uncle->color = BLACK;
                    grandparent->color = RED;

                    node = grandparent;
                }
                else
                {

                    // CASE 2: RL
                    if (node == parent->left)
                    {

                        rightRotate(parent);

                        node = parent;
                        parent = node->parent;
                    }

                    // CASE 3: RR
                    leftRotate(grandparent);

                    Color temp = parent->color;
                    parent->color = grandparent->color;
                    grandparent->color = temp;
                }
            }
        }

        root->color = BLACK;
    }

public:
    RBTree()
    {
        root = NULL;
    }

    void insert(int val)
    {

        Node *node = new Node(val);

        Node *parent = NULL;
        Node *curr = root;

        // NORMAL BST INSERT
        while (curr != NULL)
        {

            parent = curr;

            if (node->data < curr->data)
            {
                curr = curr->left;
            }
            else
            {
                curr = curr->right;
            }
        }

        node->parent = parent;

        if (parent == NULL)
        {
            root = node;
        }
        else if (node->data < parent->data)
        {
            parent->left = node;
        }
        else
        {
            parent->right = node;
        }

        // FIX RED BLACK TREE
        fixInsert(node);
    }

    void inorder(Node *node)
    {

        if (node == NULL)
            return;

        inorder(node->left);

        cout << node->data;

        if (node->color == RED)
        {
            cout << "(R) ";
        }
        else
        {
            cout << "(B) ";
        }

        inorder(node->right);
    }

    void printTree()
    {

        inorder(root);

        cout << endl;
    }
};

int main()
{

    RBTree tree;

    tree.insert(10);
    tree.insert(20);
    tree.insert(30);
    tree.insert(15);
    tree.insert(5);
    tree.insert(1);

    cout << "Red-Black Tree (Inorder Traversal):" << endl;
    tree.printTree();

    return 0;
}