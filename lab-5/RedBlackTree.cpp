#include <iostream>
#include <string>

using namespace std;

// Using strongly typed enum for enhanced code safety
enum class Color { BLACK, RED };

struct RBNode {
    int key;
    RBNode* parent;
    RBNode* left;
    RBNode* right;
    Color nodeColor;
};

class RBTreeManager {
private:
    RBNode* treeRoot;
    RBNode* nilNode; // Shared sentinel node representing leaf nodes

    // Helper method to allocate and initialize new tree nodes
    RBNode* initNode(int val) {
        RBNode* newNode = new RBNode;
        newNode->key = val;
        newNode->parent = nullptr;
        newNode->left = nilNode;
        newNode->right = nilNode;
        newNode->nodeColor = Color::RED; // New nodes are traditionally inserted as RED
        return newNode;
    }

    // Pivots a subtree to the left around pivotNode
    void executeLeftRotation(RBNode* pivotNode) {
        RBNode* childNode = pivotNode->right;
        pivotNode->right = childNode->left;

        if (childNode->left != nilNode) {
            childNode->left->parent = pivotNode;
        }

        childNode->parent = pivotNode->parent;

        if (pivotNode->parent == nullptr) {
            this->treeRoot = childNode;
        } else if (pivotNode == pivotNode->parent->left) {
            pivotNode->parent->left = childNode;
        } else {
            pivotNode->parent->right = childNode;
        }

        childNode->left = pivotNode;
        pivotNode->parent = childNode;
    }

    // Pivots a subtree to the right around pivotNode
    void executeRightRotation(RBNode* pivotNode) {
        RBNode* childNode = pivotNode->left;
        pivotNode->left = childNode->right;

        if (childNode->right != nilNode) {
            childNode->right->parent = pivotNode;
        }

        childNode->parent = pivotNode->parent;

        if (pivotNode->parent == nullptr) {
            this->treeRoot = childNode;
        } else if (pivotNode == pivotNode->parent->right) {
            pivotNode->parent->right = childNode;
        } else {
            pivotNode->parent->left = childNode;
        }

        childNode->right = pivotNode;
        pivotNode->parent = childNode;
    }

    // Restores Red-Black structural integrity after an element insertion
    void balanceAfterInsert(RBNode* targetNode) {
        RBNode* uncleNode;

        while (targetNode->parent != nullptr && targetNode->parent->nodeColor == Color::RED) {
            // Check if parent is a right child of the grandparent
            if (targetNode->parent == targetNode->parent->parent->right) {
                uncleNode = targetNode->parent->parent->left;

                if (uncleNode->nodeColor == Color::RED) {
                    // Scenario 1: Uncle is RED (Recoloring required)
                    uncleNode->nodeColor = Color::BLACK;
                    targetNode->parent->nodeColor = Color::BLACK;
                    targetNode->parent->parent->nodeColor = Color::RED;
                    targetNode = targetNode->parent->parent;
                } else {
                    // Scenario 2: Inner violation (Triangle shape)
                    if (targetNode == targetNode->parent->left) {
                        targetNode = targetNode->parent;
                        executeRightRotation(targetNode);
                    }

                    // Scenario 3: Outer violation (Line shape)
                    targetNode->parent->nodeColor = Color::BLACK;
                    targetNode->parent->parent->nodeColor = Color::RED;
                    executeLeftRotation(targetNode->parent->parent);
                }
            } else {
                // Parent is a left child of the grandparent (Mirror scenarios)
                uncleNode = targetNode->parent->parent->right;

                if (uncleNode->nodeColor == Color::RED) {
                    // Scenario 1 (Mirror): Uncle is RED
                    uncleNode->nodeColor = Color::BLACK;
                    targetNode->parent->nodeColor = Color::BLACK;
                    targetNode->parent->parent->nodeColor = Color::RED;
                    targetNode = targetNode->parent->parent;
                } else {
                    // Scenario 2 (Mirror): Inner violation
                    if (targetNode == targetNode->parent->right) {
                        targetNode = targetNode->parent;
                        executeLeftRotation(targetNode);
                    }

                    // Scenario 3 (Mirror): Outer violation
                    targetNode->parent->nodeColor = Color::BLACK;
                    targetNode->parent->parent->nodeColor = Color::RED;
                    executeRightRotation(targetNode->parent->parent);
                }
            }

            if (targetNode == treeRoot) {
                break;
            }
        }

        // Rule enforcement: Root node must be strictly black
        treeRoot->nodeColor = Color::BLACK;
    }

    // Recursive helper to build text-based tree visualization
    void renderTreeStructure(RBNode* current, string spacing, bool isLastChild) {
        if (current != nilNode) {
            cout << spacing;

            if (isLastChild) {
                cout << "R----";
                spacing += "     ";
            } else {
                cout << "L----";
                spacing += "|    ";
            }

            string printedColor = (current->nodeColor == Color::RED) ? "RED" : "BLACK";
            cout << current->key << " (" << printedColor << ")" << endl;

            renderTreeStructure(current->left, spacing, false);
            renderTreeStructure(current->right, spacing, true);
        }
    }

public:
    RBTreeManager() {
        nilNode = new RBNode;
        nilNode->nodeColor = Color::BLACK;
        nilNode->left = nullptr;
        nilNode->right = nullptr;
        treeRoot = nilNode;
    }

    // Inserts a new value into the standard BST positions before verifying colors
    void insertElement(int key) {
        RBNode* newNode = initNode(key);
        RBNode* trailingPtr = nullptr;
        RBNode* currentPtr = this->treeRoot;

        // Standard Binary Search Tree insertion trajectory
        while (currentPtr != nilNode) {
            trailingPtr = currentPtr;
            if (newNode->key < currentPtr->key) {
                currentPtr = currentPtr->left;
            } else {
                currentPtr = currentPtr->right;
            }
        }

        newNode->parent = trailingPtr;

        if (trailingPtr == nullptr) {
            treeRoot = newNode;
        } else if (newNode->key < trailingPtr->key) {
            trailingPtr->left = newNode;
        } else {
            trailingPtr->right = newNode;
        }

        // Early exits if structural checks are not necessary
        if (newNode->parent == nullptr) {
            newNode->nodeColor = Color::BLACK;
            return;
        }

        if (newNode->parent->parent == nullptr) {
            return;
        }

        // Apply fixing routines
        balanceAfterInsert(newNode);
    }

    // Public exposure for tree structural printouts
    void displayTree() {
        if (treeRoot == nilNode) {
            cout << "The tree is currently empty." << endl;
        } else {
            renderTreeStructure(this->treeRoot, "", true);
        }
    }
};

int main() {
    RBTreeManager myTree;

    cout << "Inserting elements: 55, 40, 65, 60, 75, 57\n" << endl;

    myTree.insertElement(55);
    myTree.insertElement(40);
    myTree.insertElement(65);
    myTree.insertElement(60);
    myTree.insertElement(75);
    myTree.insertElement(57);

    cout << "Red-Black Tree Layout Graph:" << endl;
    myTree.displayTree();

    return 0;
}