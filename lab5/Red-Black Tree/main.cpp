/*
 * ==========================================================
 * Lab 5 - Red Black Tree Implementation
 *
 * Name  : Patel Jash
 * Roll  : 24bcs10632
 *
 * This code implements a Red-Black Tree, a self-balancing
 * Binary Search Tree that ensures O(log n) time complexity
 * for both insertion and lookup operations.
 * ==========================================================
 */

#include <iostream>

enum NodeColor { COLOR_RED, COLOR_BLACK };

// Structure defining a single node in the Red-Black Tree
struct RBNode {
    int data;
    NodeColor nodeColor;
    RBNode* leftChild;
    RBNode* rightChild;
    RBNode* parentNode;
};

class RBTree {
public:
    RBTree() {
        // Initialize the TNULL sentinel node (acts as a black leaf)
        TNULL = new RBNode{0, COLOR_BLACK, nullptr, nullptr, nullptr};
        rootNode = TNULL;
    }

    void insertValue(int data) {
        // Newly inserted nodes are initially colored RED
        RBNode* newNode = new RBNode{data, COLOR_RED, TNULL, TNULL, TNULL};

        RBNode* parentPtr = TNULL;
        RBNode* currentPtr = rootNode;

        // Perform standard BST insertion logic
        while (currentPtr != TNULL) {
            parentPtr = currentPtr;

            if (data < currentPtr->data) {
                currentPtr = currentPtr->leftChild;
            } else {
                currentPtr = currentPtr->rightChild;
            }
        }

        newNode->parentNode = parentPtr;

        // Link the new node to its appropriate parent
        if (parentPtr == TNULL) {
            rootNode = newNode;
        } else if (data < parentPtr->data) {
            parentPtr->leftChild = newNode;
        } else {
            parentPtr->rightChild = newNode;
        }

        // Fix any potential violations of Red-Black Tree rules
        fixInsertViolations(newNode);
    }

    bool searchValue(int target) const {
        RBNode* temp = rootNode;

        // Iterative search for the target value
        while (temp != TNULL) {
            if (target == temp->data) {
                return true;
            }

            if (target < temp->data) {
                temp = temp->leftChild;
            } else {
                temp = temp->rightChild;
            }
        }

        return false;
    }

private:
    RBNode* rootNode;
    RBNode* TNULL;

    // Perform a left rotation on the specified node
    void rotateLeft(RBNode* x) {
        RBNode* y = x->rightChild;
        x->rightChild = y->leftChild;

        if (y->leftChild != TNULL) {
            y->leftChild->parentNode = x;
        }

        y->parentNode = x->parentNode;

        if (x->parentNode == TNULL) {
            rootNode = y;
        } else if (x == x->parentNode->leftChild) {
            x->parentNode->leftChild = y;
        } else {
            x->parentNode->rightChild = y;
        }

        y->leftChild = x;
        x->parentNode = y;
    }

    // Perform a right rotation on the specified node
    void rotateRight(RBNode* y) {
        RBNode* x = y->leftChild;
        y->leftChild = x->rightChild;

        if (x->rightChild != TNULL) {
            x->rightChild->parentNode = y;
        }

        x->parentNode = y->parentNode;

        if (y->parentNode == TNULL) {
            rootNode = x;
        } else if (y == y->parentNode->rightChild) {
            y->parentNode->rightChild = x;
        } else {
            y->parentNode->leftChild = x;
        }

        x->rightChild = y;
        y->parentNode = x;
    }

    // Resolves rule violations introduced by insertion
    void fixInsertViolations(RBNode* k) {
        while (k->parentNode->nodeColor == COLOR_RED) {
            RBNode* grandParent = k->parentNode->parentNode;

            if (k->parentNode == grandParent->leftChild) {
                RBNode* uncle = grandParent->rightChild;

                // Scenario 1: Uncle is RED (Recoloring needed)
                if (uncle->nodeColor == COLOR_RED) {
                    k->parentNode->nodeColor = COLOR_BLACK;
                    uncle->nodeColor = COLOR_BLACK;
                    grandParent->nodeColor = COLOR_RED;
                    k = grandParent;
                } else {
                    // Scenario 2: Node is a right child (Left rotation needed)
                    if (k == k->parentNode->rightChild) {
                        k = k->parentNode;
                        rotateLeft(k);
                    }

                    // Scenario 3: Node is a left child (Right rotation needed)
                    k->parentNode->nodeColor = COLOR_BLACK;
                    grandParent->nodeColor = COLOR_RED;
                    rotateRight(grandParent);
                }
            } else {
                // Symmetric cases for when the parent is a right child
                RBNode* uncle = grandParent->leftChild;

                // Scenario 1: Uncle is RED
                if (uncle->nodeColor == COLOR_RED) {
                    k->parentNode->nodeColor = COLOR_BLACK;
                    uncle->nodeColor = COLOR_BLACK;
                    grandParent->nodeColor = COLOR_RED;
                    k = grandParent;
                } else {
                    // Scenario 2: Node is a left child (Right rotation needed)
                    if (k == k->parentNode->leftChild) {
                        k = k->parentNode;
                        rotateRight(k);
                    }

                    // Scenario 3: Node is a right child (Left rotation needed)
                    k->parentNode->nodeColor = COLOR_BLACK;
                    grandParent->nodeColor = COLOR_RED;
                    rotateLeft(grandParent);
                }
            }
        }

        // The root must ALWAYS be black
        rootNode->nodeColor = COLOR_BLACK;
    }

public:
    // Initiates inorder traversal printing
    void displaySorted() const {
        printInorder(rootNode);
        std::cout << std::endl;
    }

private:
    void printInorder(RBNode* node) const {
        if (node == TNULL) {
            return;
        }

        printInorder(node->leftChild);

        std::cout << node->data 
                  << (node->nodeColor == COLOR_RED ? "[Red] " : "[Blk] ");

        printInorder(node->rightChild);
    }
};

int main() {
    std::cout << "--- Red-Black Tree Implementation ---\n";
    std::cout << "Student: Patel Jash | Roll: 24bcs10632\n\n";

    RBTree rbt;

    int valuesToInsert[] = {55, 40, 65, 60, 75, 57};
    std::cout << "Inserting elements: ";
    for (int val : valuesToInsert) {
        std::cout << val << " ";
        rbt.insertValue(val);
    }
    std::cout << "\n\nInorder Traversal after insertions:\n";
    rbt.displaySorted();

    int searchKeys[] = {60, 99};
    for (int key : searchKeys) {
        if (rbt.searchValue(key)) {
            std::cout << "Value " << key << " exists in the tree.\n";
        } else {
            std::cout << "Value " << key << " is missing from the tree.\n";
        }
    }

    return 0;
}