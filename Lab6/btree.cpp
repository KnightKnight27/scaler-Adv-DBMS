#include <iostream>

using namespace std;

class BTreeNode {
    int* keyArray;
    int minDegree;
    BTreeNode** childPtrs;
    int numKeys;
    bool isLeaf;

public:
    BTreeNode(int degree, bool leafNode) {
        minDegree = degree;
        isLeaf = leafNode;
        
        keyArray = new int[2 * minDegree - 1];
        childPtrs = new BTreeNode*[2 * minDegree];
        numKeys = 0;
    }

    ~BTreeNode() {
        delete[] keyArray;
        if (!isLeaf) {
            for (int i = 0; i <= numKeys; i++) {
                delete childPtrs[i];
            }
        }
        delete[] childPtrs;
    }

    void display() {
        int i;
        for (i = 0; i < numKeys; i++) {
            if (!isLeaf) {
                childPtrs[i]->display();
            }
            cout << keyArray[i] << " ";
        }

        if (!isLeaf) {
            childPtrs[i]->display();
        }
    }

    BTreeNode* find(int targetKey) {
        int idx = 0;
        while (idx < numKeys && targetKey > keyArray[idx]) {
            idx++;
        }

        if (idx < numKeys && keyArray[idx] == targetKey) {
            return this;
        }

        if (isLeaf) {
            return nullptr;
        }

        return childPtrs[idx]->find(targetKey);
    }

    void insertWhenNotFull(int newKey) {
        int idx = numKeys - 1;

        if (isLeaf) {
            while (idx >= 0 && keyArray[idx] > newKey) {
                keyArray[idx + 1] = keyArray[idx];
                idx--;
            }

            keyArray[idx + 1] = newKey;
            numKeys++;
        } else {
            while (idx >= 0 && keyArray[idx] > newKey) {
                idx--;
            }

            if (childPtrs[idx + 1]->numKeys == 2 * minDegree - 1) {
                splitChildNode(idx + 1, childPtrs[idx + 1]);

                if (keyArray[idx + 1] < newKey) {
                    idx++;
                }
            }
            childPtrs[idx + 1]->insertWhenNotFull(newKey);
        }
    }

    void splitChildNode(int childIndex, BTreeNode* fullChild) {
        BTreeNode* newNode = new BTreeNode(fullChild->minDegree, fullChild->isLeaf);
        newNode->numKeys = minDegree - 1;

        for (int j = 0; j < minDegree - 1; j++) {
            newNode->keyArray[j] = fullChild->keyArray[j + minDegree];
        }

        if (!fullChild->isLeaf) {
            for (int j = 0; j < minDegree; j++) {
                newNode->childPtrs[j] = fullChild->childPtrs[j + minDegree];
            }
        }

        fullChild->numKeys = minDegree - 1;

        for (int j = numKeys; j >= childIndex + 1; j--) {
            childPtrs[j + 1] = childPtrs[j];
        }
        childPtrs[childIndex + 1] = newNode;

        for (int j = numKeys - 1; j >= childIndex; j--) {
            keyArray[j + 1] = keyArray[j];
        }

        keyArray[childIndex] = fullChild->keyArray[minDegree - 1];
        numKeys++;
    }

    friend class BTree;
};

class BTree {
    BTreeNode* rootNode;
    int minDegree;

public:
    BTree(int degree) {
        rootNode = nullptr;
        minDegree = degree;
    }

    ~BTree() {
        delete rootNode;
    }

    void displayTree() {
        if (rootNode != nullptr) {
            rootNode->display();
        }
    }

    BTreeNode* searchKey(int targetKey) {
        if (rootNode == nullptr) {
            return nullptr;
        }
        return rootNode->find(targetKey);
    }

    void insertKey(int newKey) {
        if (rootNode == nullptr) {
            rootNode = new BTreeNode(minDegree, true);
            rootNode->keyArray[0] = newKey;
            rootNode->numKeys = 1;
        } else {
            if (rootNode->numKeys == 2 * minDegree - 1) {
                BTreeNode* newRoot = new BTreeNode(minDegree, false);
                
                newRoot->childPtrs[0] = rootNode;
                
                newRoot->splitChildNode(0, rootNode);

                int idx = 0;
                if (newRoot->keyArray[0] < newKey) {
                    idx++;
                }

                newRoot->childPtrs[idx]->insertWhenNotFull(newKey);
                
                rootNode = newRoot;
            } else {
                rootNode->insertWhenNotFull(newKey);
            }
        }
    }
};

int main() {
    int degreeInput;
    cout << "Enter the minimum degree (t) for the B-Tree: ";
    if (!(cin >> degreeInput)) return 0;

    BTree myTree(degreeInput);

    int userChoice, val;

    while (true) {
        cout << "\n--- B-Tree Menu ---\n";
        cout << "1. Insert a key\n";
        cout << "2. Search for a key\n";
        cout << "3. Display the tree (Inorder)\n";
        cout << "4. Exit\n";
        cout << "Enter your choice: ";
        
        if (!(cin >> userChoice)) break;

        if (userChoice == 4) {
            cout << "Exiting...\n";
            break;
        }

        switch (userChoice) {
            case 1:
                cout << "Enter value to insert: ";
                if (cin >> val) {
                    myTree.insertKey(val);
                    cout << "Inserted " << val << " successfully.\n";
                }
                break;

            case 2:
                cout << "Enter value to search: ";
                if (cin >> val) {
                    if (myTree.searchKey(val) != nullptr) {
                        cout << "Key " << val << " was FOUND in the tree.\n";
                    } else {
                        cout << "Key " << val << " was NOT FOUND.\n";
                    }
                }
                break;

            case 3:
                cout << "Tree contents: ";
                myTree.displayTree();
                cout << endl;
                break;
                
            default:
                cout << "Invalid choice! Please try again.\n";
                break;
        }
    }

    return 0;
}
