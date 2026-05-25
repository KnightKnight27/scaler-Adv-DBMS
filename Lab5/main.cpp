#include <iostream>

using namespace std;

enum NodeColor
{
    RED_NODE,
    BLACK_NODE
};

template <class T>
class RBTree
{
private:

    struct TreeNode
    {
        T value;
        NodeColor shade;

        TreeNode* leftChild;
        TreeNode* rightChild;
        TreeNode* parentNode;

        TreeNode(T val)
        {
            value = val;
            shade = RED_NODE;

            leftChild = nullptr;
            rightChild = nullptr;
            parentNode = nullptr;
        }
    };

    TreeNode* treeRoot;

private:

    void rotateLeft(TreeNode* current)
    {
        TreeNode* temp = current->rightChild;

        current->rightChild = temp->leftChild;

        if(temp->leftChild != nullptr)
        {
            temp->leftChild->parentNode = current;
        }

        temp->parentNode = current->parentNode;

        if(current->parentNode == nullptr)
        {
            treeRoot = temp;
        }
        else if(current == current->parentNode->leftChild)
        {
            current->parentNode->leftChild = temp;
        }
        else
        {
            current->parentNode->rightChild = temp;
        }

        temp->leftChild = current;
        current->parentNode = temp;
    }

    void rotateRight(TreeNode* current)
    {
        TreeNode* temp = current->leftChild;

        current->leftChild = temp->rightChild;

        if(temp->rightChild != nullptr)
        {
            temp->rightChild->parentNode = current;
        }

        temp->parentNode = current->parentNode;

        if(current->parentNode == nullptr)
        {
            treeRoot = temp;
        }
        else if(current == current->parentNode->leftChild)
        {
            current->parentNode->leftChild = temp;
        }
        else
        {
            current->parentNode->rightChild = temp;
        }

        temp->rightChild = current;
        current->parentNode = temp;
    }

    void balanceAfterInsert(TreeNode* current)
    {
        while(current != treeRoot &&
              current->parentNode->shade == RED_NODE)
        {
            TreeNode* parent = current->parentNode;
            TreeNode* grand = parent->parentNode;

            if(parent == grand->leftChild)
            {
                TreeNode* uncle = grand->rightChild;

                if(uncle != nullptr && uncle->shade == RED_NODE)
                {
                    parent->shade = BLACK_NODE;
                    uncle->shade = BLACK_NODE;
                    grand->shade = RED_NODE;

                    current = grand;
                }
                else
                {
                    if(current == parent->rightChild)
                    {
                        current = parent;
                        rotateLeft(current);
                    }

                    parent->shade = BLACK_NODE;
                    grand->shade = RED_NODE;

                    rotateRight(grand);
                }
            }
            else
            {
                TreeNode* uncle = grand->leftChild;

                if(uncle != nullptr && uncle->shade == RED_NODE)
                {
                    parent->shade = BLACK_NODE;
                    uncle->shade = BLACK_NODE;
                    grand->shade = RED_NODE;

                    current = grand;
                }
                else
                {
                    if(current == parent->leftChild)
                    {
                        current = parent;
                        rotateRight(current);
                    }

                    parent->shade = BLACK_NODE;
                    grand->shade = RED_NODE;

                    rotateLeft(grand);
                }
            }
        }

        treeRoot->shade = BLACK_NODE;
    }

    void printInorder(TreeNode* node)
    {
        if(node == nullptr)
        {
            return;
        }

        printInorder(node->leftChild);

        cout
            << node->value
            << " ["
            << (node->shade == RED_NODE ? "R" : "B")
            << "] ";

        printInorder(node->rightChild);
    }

public:

    RBTree()
    {
        treeRoot = nullptr;
    }

    void add(T val)
    {
        TreeNode* node = new TreeNode(val);

        TreeNode* previous = nullptr;
        TreeNode* traversal = treeRoot;

        while(traversal != nullptr)
        {
            previous = traversal;

            if(val < traversal->value)
            {
                traversal = traversal->leftChild;
            }
            else
            {
                traversal = traversal->rightChild;
            }
        }

        node->parentNode = previous;

        if(previous == nullptr)
        {
            treeRoot = node;
        }
        else if(val < previous->value)
        {
            previous->leftChild = node;
        }
        else
        {
            previous->rightChild = node;
        }

        balanceAfterInsert(node);
    }

    void show()
    {
        printInorder(treeRoot);
        cout << endl;
    }
};

int main()
{
    RBTree<int> tree;

    tree.add(10);
    tree.add(20);
    tree.add(30);
    tree.add(15);
    tree.add(5);
    tree.add(1);

    tree.show();

    return 0;
}