#include <iostream>
using namespace std;

struct RBTNode
{
    int data; 
    bool isRed;
    RBTNode *left, *right, *parent;

    RBTNode(int value) {
        data = value; 
        isRed = true; 
        left = right = parent = nullptr;
    }
};

class RedBlackTree
{
    RBTNode* treeRoot = nullptr;

    void rotateLeft(RBTNode* currNode)
    {
        RBTNode* childNode = currNode->right;
        currNode->right = childNode->left;
        if(childNode->left) childNode->left->parent = currNode;
        childNode->parent = currNode->parent;

        if(!currNode->parent) treeRoot = childNode;
        else if(currNode == currNode->parent->left) currNode->parent->left = childNode;
        else currNode->parent->right = childNode;
        childNode->left = currNode;
        currNode->parent = childNode;
    }

    void rotateRight(RBTNode* currNode)
    {
        RBTNode* childNode = currNode->left;
        currNode->left = childNode->right;
        if(childNode->right) childNode->right->parent = currNode;
        childNode->parent = currNode->parent;

        if(!currNode->parent) treeRoot = childNode;
        else if(currNode == currNode->parent->left) currNode->parent->left = childNode;
        else currNode->parent->right = childNode;
        childNode->right = currNode;
        currNode->parent = childNode;
    }

    void balanceInsertion(RBTNode* targetNode)
    {
        while(targetNode != treeRoot && targetNode->parent->isRed)
        {
            RBTNode* parentNode = targetNode->parent;
            RBTNode* grandParent = parentNode->parent;
    
            if(parentNode == grandParent->left)
            {
                RBTNode* uncleNode = grandParent->right;
    
                if(uncleNode && uncleNode->isRed)
                {
                    parentNode->isRed = false;
                    uncleNode->isRed = false;
                    grandParent->isRed = true;
                    targetNode = grandParent;
                }
                else
                {
                    if(targetNode == parentNode->right)
                    {
                        rotateLeft(parentNode);
                        targetNode = parentNode;
                        parentNode = targetNode->parent;
                    }
    
                    rotateRight(grandParent);
    
                    parentNode->isRed = false;
                    grandParent->isRed = true;
                }
            }
            else
            {
                RBTNode* uncleNode = grandParent->left;
    
                if(uncleNode && uncleNode->isRed)
                {
                    parentNode->isRed = false;
                    uncleNode->isRed = false;
                    grandParent->isRed = true;
                    targetNode = grandParent;
                }
                else
                {
                    if(targetNode == parentNode->left)
                    {
                        rotateRight(parentNode);
                        targetNode = parentNode;
                        parentNode = targetNode->parent;
                    }
    
                    rotateLeft(grandParent);
                    parentNode->isRed = false;
                    grandParent->isRed = true;
                }
            }
        }
    
        treeRoot->isRed = false;
    }
};