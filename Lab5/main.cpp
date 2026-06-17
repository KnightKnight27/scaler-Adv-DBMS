#include <iostream>

using namespace std;

enum NodeColor {
    RED_COLOR,
    BLACK_COLOR
};

template<typename T>
class MyRBTree {
private:
    struct TreeNode {
        T value;
        NodeColor nodeColor;

        TreeNode* leftChild;
        TreeNode* rightChild;
        TreeNode* parentNode;

        TreeNode(T val) {
            value = val;
            nodeColor = RED_COLOR; 
            leftChild = nullptr;
            rightChild = nullptr;
            parentNode = nullptr;
        }
    };

    TreeNode* rootNode = nullptr;

    void rotateLeft(TreeNode* current) {
        TreeNode* rightSubtree = current->rightChild;

        current->rightChild = rightSubtree->leftChild;

        if (rightSubtree->leftChild != nullptr) {
            rightSubtree->leftChild->parentNode = current;
        }

        rightSubtree->parentNode = current->parentNode;

        if (current->parentNode == nullptr) {
            rootNode = rightSubtree;
        } 
        else if (current == current->parentNode->leftChild) {
            current->parentNode->leftChild = rightSubtree;
        } 
        else {
            current->parentNode->rightChild = rightSubtree;
        }

        rightSubtree->leftChild = current;
        current->parentNode = rightSubtree;
    }

    void rotateRight(TreeNode* current) {
        TreeNode* leftSubtree = current->leftChild;

        current->leftChild = leftSubtree->rightChild;

        if (leftSubtree->rightChild != nullptr) {
            leftSubtree->rightChild->parentNode = current;
        }

        leftSubtree->parentNode = current->parentNode;

        if (current->parentNode == nullptr) {
            rootNode = leftSubtree;
        } 
        else if (current == current->parentNode->leftChild) {
            current->parentNode->leftChild = leftSubtree;
        } 
        else {
            current->parentNode->rightChild = leftSubtree;
        }

        leftSubtree->rightChild = current;
        current->parentNode = leftSubtree;
    }

    void balanceAfterInsertion(TreeNode* target) {
        // Keep fixing while we are not at the root and our parent is RED
        // (which violates the rule that RED nodes cannot have RED children)
        while (target != rootNode && target->parentNode->nodeColor == RED_COLOR) {
            
            TreeNode* parent = target->parentNode;
            TreeNode* grandParent = parent->parentNode;

            // Parent is the left child of the grandparent
            if (parent == grandParent->leftChild) {
                TreeNode* uncleNode = grandParent->rightChild;

                // Case 1: Uncle is RED. We just recolor.
                if (uncleNode != nullptr && uncleNode->nodeColor == RED_COLOR) {
                    parent->nodeColor = BLACK_COLOR;
                    uncleNode->nodeColor = BLACK_COLOR;
                    grandParent->nodeColor = RED_COLOR;
                    
                    // Move the target up to check the grandparent
                    target = grandParent;
                } 
                else {
                    // Case 2: Target is a right child (Triangle case)
                    if (target == parent->rightChild) {
                        target = parent;
                        rotateLeft(target);
                    }

                    // Case 3: Target is a left child (Line case)
                    parent->nodeColor = BLACK_COLOR;
                    grandParent->nodeColor = RED_COLOR;
                    rotateRight(grandParent);
                }
            } 
            // Parent is the right child of the grandparent (symmetric to above)
            else {
                TreeNode* uncleNode = grandParent->leftChild;

                if (uncleNode != nullptr && uncleNode->nodeColor == RED_COLOR) {
                    parent->nodeColor = BLACK_COLOR;
                    uncleNode->nodeColor = BLACK_COLOR;
                    grandParent->nodeColor = RED_COLOR;
                    
                    target = grandParent;
                } 
                else {
                    if (target == parent->leftChild) {
                        target = parent;
                        rotateRight(target);
                    }

                    parent->nodeColor = BLACK_COLOR;
                    grandParent->nodeColor = RED_COLOR;
                    rotateLeft(grandParent);
                }
            }
        }

        // The root must always be black
        rootNode->nodeColor = BLACK_COLOR;
    }

    void printInOrder(TreeNode* node) {
        if (node == nullptr) {
            return;
        }

        printInOrder(node->leftChild);

        cout << node->value << (node->nodeColor == RED_COLOR ? " [Red] " : " [Black] ");

        printInOrder(node->rightChild);
    }

public:
    void insertValue(T val) {
        TreeNode* createdNode = new TreeNode(val);

        TreeNode* parentTracker = nullptr;
        TreeNode* traverseNode = rootNode;

        while (traverseNode != nullptr) {
            parentTracker = traverseNode;

            if (val < traverseNode->value) {
                traverseNode = traverseNode->leftChild;
            } else {
                traverseNode = traverseNode->rightChild;
            }
        }

        createdNode->parentNode = parentTracker;

        if (parentTracker == nullptr) {
            rootNode = createdNode;
        } 
        else if (val < parentTracker->value) {
            parentTracker->leftChild = createdNode;
        } 
        else {
            parentTracker->rightChild = createdNode;
        }

        balanceAfterInsertion(createdNode);
    }

    void showTree() {
        printInOrder(rootNode);
        cout << endl;
    }
};

int main() {
    MyRBTree<int> rbTreeObj;

    rbTreeObj.insertValue(10);
    rbTreeObj.insertValue(20);
    rbTreeObj.insertValue(30);
    rbTreeObj.insertValue(15);
    rbTreeObj.insertValue(5);
    rbTreeObj.insertValue(1);

    rbTreeObj.showTree();

    return 0;
}