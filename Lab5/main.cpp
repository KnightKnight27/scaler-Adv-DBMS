#include <iostream>

using namespace std;

enum Color
{
    RED,
    BLACK
};

template<typename T>
class RedBlackTree
{
private:

    struct Node
    {
        T data;
        Color color;

        Node* left;
        Node* right;
        Node* parent;

        Node(T value)
        {
            data = value;

            color = RED;

            left = nullptr;
            right = nullptr;
            parent = nullptr;
        }
    };

    Node* root = nullptr;

private:

    void leftRotate(Node* x)
    {
        Node* y = x->right;

        x->right = y->left;

        if(y->left != nullptr)
            y->left->parent = x;

        y->parent = x->parent;

        if(x->parent == nullptr)
            root = y;

        else if(x == x->parent->left)
            x->parent->left = y;

        else
            x->parent->right = y;

        y->left = x;

        x->parent = y;
    }

    void rightRotate(Node* y)
    {
        Node* x = y->left;

        y->left = x->right;

        if(x->right != nullptr)
            x->right->parent = y;

        x->parent = y->parent;

        if(y->parent == nullptr)
            root = x;

        else if(y == y->parent->left)
            y->parent->left = x;

        else
            y->parent->right = x;

        x->right = y;

        y->parent = x;
    }

    void fixInsert(Node* node)
    {
        while(node != root &&
              node->parent->color == RED)
        {
            Node* parent = node->parent;
            Node* grandParent = parent->parent;

            if(parent == grandParent->left)
            {
                Node* uncle = grandParent->right;

                if(uncle != nullptr &&
                   uncle->color == RED)
                {
                    parent->color = BLACK;
                    uncle->color = BLACK;
                    grandParent->color = RED;

                    node = grandParent;
                }
                else
                {
                    if(node == parent->right)
                    {
                        node = parent;
                        leftRotate(node);
                    }

                    parent->color = BLACK;
                    grandParent->color = RED;

                    rightRotate(grandParent);
                }
            }
            else
            {
                Node* uncle = grandParent->left;

                if(uncle != nullptr &&
                   uncle->color == RED)
                {
                    parent->color = BLACK;
                    uncle->color = BLACK;
                    grandParent->color = RED;

                    node = grandParent;
                }
                else
                {
                    if(node == parent->left)
                    {
                        node = parent;
                        rightRotate(node);
                    }

                    parent->color = BLACK;
                    grandParent->color = RED;

                    leftRotate(grandParent);
                }
            }
        }

        root->color = BLACK;
    }

    void inorder(Node* node)
    {
        if(node == nullptr)
            return;

        inorder(node->left);

        cout
            << node->data
            << " ("
            << (node->color == RED ? "R" : "B")
            << ") ";

        inorder(node->right);
    }

public:

    void insert(T value)
    {
        Node* newNode = new Node(value);

        Node* parent = nullptr;
        Node* current = root;

        while(current != nullptr)
        {
            parent = current;

            if(value < current->data)
                current = current->left;

            else
                current = current->right;
        }

        newNode->parent = parent;

        if(parent == nullptr)
            root = newNode;

        else if(value < parent->data)
            parent->left = newNode;

        else
            parent->right = newNode;

        fixInsert(newNode);
    }

    void display()
    {
        inorder(root);

        cout << endl;
    }
};

int main()
{
    RedBlackTree<int> rbTree;

    rbTree.insert(10);
    rbTree.insert(20);
    rbTree.insert(30);
    rbTree.insert(15);
    rbTree.insert(5);
    rbTree.insert(1);

    rbTree.display();

    return 0;
}
