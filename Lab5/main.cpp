#include <iostream>

using namespace std;

template <typename T>
class RBTree
{
private:

    enum NodeColor
    {
        RED,
        BLACK
    };

    struct TreeNode
    {
        T value;
        NodeColor clr;

        TreeNode* leftChild;
        TreeNode* rightChild;
        TreeNode* parentNode;

        TreeNode(T val)
        {
            value = val;
            clr = RED;

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
            temp->leftChild->parentNode = current;

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
            temp->rightChild->parentNode = current;

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

    void balanceTree(TreeNode* node)
    {
        while(node != treeRoot &&
              node->parentNode->clr == RED)
        {
            TreeNode* parent = node->parentNode;
            TreeNode* grand = parent->parentNode;

            if(parent == grand->leftChild)
            {
                TreeNode* uncle = grand->rightChild;

                if(uncle != nullptr && uncle->clr == RED)
                {
                    parent->clr = BLACK;
                    uncle->clr = BLACK;
                    grand->clr = RED;

                    node = grand;
                }
                else
                {
                    if(node == parent->rightChild)
                    {
                        node = parent;
                        rotateLeft(node);
                    }

                    parent->clr = BLACK;
                    grand->clr = RED;

                    rotateRight(grand);
                }
            }
            else
            {
                TreeNode* uncle = grand->leftChild;

                if(uncle != nullptr && uncle->clr == RED)
                {
                    parent->clr = BLACK;
                    uncle->clr = BLACK;
                    grand->clr = RED;

                    node = grand;
                }
                else
                {
                    if(node == parent->leftChild)
                    {
                        node = parent;
                        rotateRight(node);
                    }

                    parent->clr = BLACK;
                    grand->clr = RED;

                    rotateLeft(grand);
                }
            }
        }

        treeRoot->clr = BLACK;
    }

    void printInorder(TreeNode* node)
    {
        if(node == nullptr)
            return;

        printInorder(node->leftChild);

        cout << node->value
             << "["
             << (node->clr == RED ? "R" : "B")
             << "] ";

        printInorder(node->rightChild);
    }

public:

    RBTree()
    {
        treeRoot = nullptr;
    }

    void addNode(T val)
    {
        TreeNode* newNode = new TreeNode(val);

        TreeNode* temp = treeRoot;
        TreeNode* parent = nullptr;

        while(temp != nullptr)
        {
            parent = temp;

            if(val < temp->value)
                temp = temp->leftChild;
            else
                temp = temp->rightChild;
        }

        newNode->parentNode = parent;

        if(parent == nullptr)
        {
            treeRoot = newNode;
        }
        else if(val < parent->value)
        {
            parent->leftChild = newNode;
        }
        else
        {
            parent->rightChild = newNode;
        }

        balanceTree(newNode);
    }

    void showTree()
    {
        printInorder(treeRoot);
        cout << endl;
    }
};

int main()
{
    RBTree<int> tree;

    tree.addNode(50);
    tree.addNode(25);
    tree.addNode(75);
    tree.addNode(10);
    tree.addNode(40);
    tree.addNode(60);
    tree.addNode(90);

    tree.showTree();

    return 0;
}