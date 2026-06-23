#include <iostream>
using namespace std;

enum NodeColor
{
    RED,
    BLACK
};

template<typename T>
class RBTree
{
private:

    struct TreeNode
    {
        T val;
        NodeColor clr;

        TreeNode* leftChild;
        TreeNode* rightChild;
        TreeNode* par;

        TreeNode(T x)
        {
            val = x;
            clr = RED;

            leftChild = nullptr;
            rightChild = nullptr;
            par = nullptr;
        }
    };

    TreeNode* rootNode = nullptr;

private:

    void rotateLeft(TreeNode* curr)
    {
        TreeNode* child = curr->rightChild;

        curr->rightChild = child->leftChild;

        if(child->leftChild != nullptr)
            child->leftChild->par = curr;

        child->par = curr->par;

        if(curr->par == nullptr)
            rootNode = child;

        else if(curr == curr->par->leftChild)
            curr->par->leftChild = child;

        else
            curr->par->rightChild = child;

        child->leftChild = curr;
        curr->par = child;
    }

    void rotateRight(TreeNode* curr)
    {
        TreeNode* child = curr->leftChild;

        curr->leftChild = child->rightChild;

        if(child->rightChild != nullptr)
            child->rightChild->par = curr;

        child->par = curr->par;

        if(curr->par == nullptr)
            rootNode = child;

        else if(curr == curr->par->leftChild)
            curr->par->leftChild = child;

        else
            curr->par->rightChild = child;

        child->rightChild = curr;
        curr->par = child;
    }

    void balanceAfterInsert(TreeNode* node)
    {
        while(node != rootNode &&
              node->par->clr == RED)
        {
            TreeNode* parent = node->par;
            TreeNode* grand = parent->par;

            if(parent == grand->leftChild)
            {
                TreeNode* uncle = grand->rightChild;

                if(uncle != nullptr &&
                   uncle->clr == RED)
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

                if(uncle != nullptr &&
                   uncle->clr == RED)
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

        rootNode->clr = BLACK;
    }

    void printInorder(TreeNode* node)
    {
        if(node == nullptr)
            return;

        printInorder(node->leftChild);

        cout
            << node->val
            << "["
            << (node->clr == RED ? "R" : "B")
            << "] ";

        printInorder(node->rightChild);
    }

public:

    void insertValue(T value)
    {
        TreeNode* newNode = new TreeNode(value);

        TreeNode* prev = nullptr;
        TreeNode* temp = rootNode;

        while(temp != nullptr)
        {
            prev = temp;

            if(value < temp->val)
                temp = temp->leftChild;

            else
                temp = temp->rightChild;
        }

        newNode->par = prev;

        if(prev == nullptr)
            rootNode = newNode;

        else if(value < prev->val)
            prev->leftChild = newNode;

        else
            prev->rightChild = newNode;

        balanceAfterInsert(newNode);
    }

    void show()
    {
        printInorder(rootNode);
        cout << '\n';
    }
};

int main()
{
    RBTree<int> tree;

    // testing karne ke liye values add ki
    tree.insertValue(10);
    tree.insertValue(20);
    tree.insertValue(30);
    tree.insertValue(15);
    tree.insertValue(5);
    tree.insertValue(1);

    tree.show();

    return 0;
}