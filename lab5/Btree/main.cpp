/*
 * ==========================================================
 * Lab 5 - B-Tree Index Implementation
 *
 * Name  : Patel Jash
 * Roll  : 24bcs10632
 *
 * A multi-way search tree that balances itself automatically,
 * widely utilized in file systems and databases. Nodes store
 * multiple elements, flattening the tree and accelerating
 * lookups by minimizing traversal depth.
 *
 * Supported Actions:
 * - Insert Key
 * - Find Key
 * - Delete Key
 * - Level Display Traversal
 * - Sorted Display
 * ==========================================================
 */

#include <iostream>
#include <vector>

class BTreeIndex {
public:
    explicit BTreeIndex(int degree) : minDeg(degree < 2 ? 2 : degree), rootNode(nullptr) {}
    ~BTreeIndex() { clearTree(rootNode); }

    void insertKey(int targetValue) {
        if (rootNode == nullptr) {
            rootNode = new BTreeNode(true);
            rootNode->elements.push_back(targetValue);
            return;
        }

        if (isNodeFull(rootNode)) {
            BTreeNode* newRoot = new BTreeNode(false);
            newRoot->children.push_back(rootNode);
            splitChildNode(newRoot, 0);
            rootNode = newRoot;
        }

        insertToNotFull(rootNode, targetValue);
    }

    bool findKey(int targetValue) const {
        return searchRecursive(rootNode, targetValue);
    }

    void deleteKey(int targetValue) {
        if (rootNode == nullptr) return;

        removeValue(rootNode, targetValue);

        if (rootNode->elements.empty() && !rootNode->isLeafNode) {
            BTreeNode* oldRoot = rootNode;
            rootNode = rootNode->children[0];
            oldRoot->children.clear();
            delete oldRoot;
        } else if (rootNode->elements.empty() && rootNode->isLeafNode) {
            delete rootNode;
            rootNode = nullptr;
        }
    }

    void displayLevels() const {
        if (!rootNode) {
            std::cout << "(Tree is empty)\n";
            return;
        }

        std::vector<BTreeNode*> currentLevel = {rootNode};
        int currentDepth = 0;

        while (!currentLevel.empty()) {
            std::cout << "L" << currentDepth << ": ";
            std::vector<BTreeNode*> nextLevel;

            for (BTreeNode* node : currentLevel) {
                std::cout << "[";
                for (size_t idx = 0; idx < node->elements.size(); ++idx) {
                    std::cout << node->elements[idx]
                              << (idx + 1 < node->elements.size() ? " " : "");
                }
                std::cout << "] ";

                for (BTreeNode* child : node->children) {
                    nextLevel.push_back(child);
                }
            }

            std::cout << "\n";
            currentLevel = nextLevel;
            ++currentDepth;
        }
    }

    void displaySorted() const {
        inorderTraversal(rootNode);
        std::cout << "\n";
    }

private:
    struct BTreeNode {
        std::vector<int> elements;
        std::vector<BTreeNode*> children;
        bool isLeafNode;

        explicit BTreeNode(bool leafFlag) : isLeafNode(leafFlag) {}
    };

    int minDeg;
    BTreeNode* rootNode;

    bool isNodeFull(BTreeNode* node) const {
        return static_cast<int>(node->elements.size()) == (2 * minDeg - 1);
    }

    void clearTree(BTreeNode* node) {
        if (!node) return;
        for (BTreeNode* child : node->children) {
            clearTree(child);
        }
        delete node;
    }

    bool searchRecursive(BTreeNode* node, int targetValue) const {
        if (!node) return false;

        size_t idx = 0;
        while (idx < node->elements.size() && targetValue > node->elements[idx]) {
            ++idx;
        }

        if (idx < node->elements.size() && targetValue == node->elements[idx]) {
            return true;
        }

        if (node->isLeafNode) {
            return false;
        }

        return searchRecursive(node->children[idx], targetValue);
    }

    void splitChildNode(BTreeNode* parentNode, int childIndex) {
        BTreeNode* fullChild = parentNode->children[childIndex];
        BTreeNode* newChild = new BTreeNode(fullChild->isLeafNode);

        int medianVal = fullChild->elements[minDeg - 1];

        newChild->elements.assign(fullChild->elements.begin() + minDeg, fullChild->elements.end());
        fullChild->elements.resize(minDeg - 1);

        if (!fullChild->isLeafNode) {
            newChild->children.assign(fullChild->children.begin() + minDeg, fullChild->children.end());
            fullChild->children.resize(minDeg);
        }

        parentNode->children.insert(parentNode->children.begin() + childIndex + 1, newChild);
        parentNode->elements.insert(parentNode->elements.begin() + childIndex, medianVal);
    }

    void insertToNotFull(BTreeNode* node, int targetValue) {
        int idx = static_cast<int>(node->elements.size()) - 1;

        if (node->isLeafNode) {
            node->elements.push_back(0);

            while (idx >= 0 && targetValue < node->elements[idx]) {
                node->elements[idx + 1] = node->elements[idx];
                --idx;
            }
            node->elements[idx + 1] = targetValue;
            return;
        }

        while (idx >= 0 && targetValue < node->elements[idx]) {
            --idx;
        }
        ++idx;

        if (isNodeFull(node->children[idx])) {
            splitChildNode(node, idx);
            if (targetValue > node->elements[idx]) {
                ++idx;
            }
        }

        insertToNotFull(node->children[idx], targetValue);
    }

    int getMaximumKey(BTreeNode* node) const {
        while (!node->isLeafNode) {
            node = node->children.back();
        }
        return node->elements.back();
    }

    int getMinimumKey(BTreeNode* node) const {
        while (!node->isLeafNode) {
            node = node->children.front();
        }
        return node->elements.front();
    }

    void mergeNodes(BTreeNode* node, int idx) {
        BTreeNode* leftChild = node->children[idx];
        BTreeNode* rightChild = node->children[idx + 1];

        leftChild->elements.push_back(node->elements[idx]);
        leftChild->elements.insert(leftChild->elements.end(), rightChild->elements.begin(), rightChild->elements.end());

        if (!leftChild->isLeafNode) {
            leftChild->children.insert(leftChild->children.end(), rightChild->children.begin(), rightChild->children.end());
        }

        node->elements.erase(node->elements.begin() + idx);
        node->children.erase(node->children.begin() + idx + 1);

        rightChild->children.clear();
        delete rightChild;
    }

    void fulfillMinimumKeys(BTreeNode* node, int idx) {
        if (static_cast<int>(node->children[idx]->elements.size()) >= minDeg) return;

        if (idx > 0 && static_cast<int>(node->children[idx - 1]->elements.size()) >= minDeg) {
            BTreeNode* targetChild = node->children[idx];
            BTreeNode* siblingNode = node->children[idx - 1];

            targetChild->elements.insert(targetChild->elements.begin(), node->elements[idx - 1]);
            node->elements[idx - 1] = siblingNode->elements.back();
            siblingNode->elements.pop_back();

            if (!targetChild->isLeafNode) {
                targetChild->children.insert(targetChild->children.begin(), siblingNode->children.back());
                siblingNode->children.pop_back();
            }
        } else if (idx < static_cast<int>(node->elements.size()) && static_cast<int>(node->children[idx + 1]->elements.size()) >= minDeg) {
            BTreeNode* targetChild = node->children[idx];
            BTreeNode* siblingNode = node->children[idx + 1];

            targetChild->elements.push_back(node->elements[idx]);
            node->elements[idx] = siblingNode->elements.front();
            siblingNode->elements.erase(siblingNode->elements.begin());

            if (!targetChild->isLeafNode) {
                targetChild->children.push_back(siblingNode->children.front());
                siblingNode->children.erase(siblingNode->children.begin());
            }
        } else {
            if (idx < static_cast<int>(node->elements.size())) {
                mergeNodes(node, idx);
            } else {
                mergeNodes(node, idx - 1);
            }
        }
    }

    void removeValue(BTreeNode* node, int targetValue) {
        int idx = 0;

        while (idx < static_cast<int>(node->elements.size()) && targetValue > node->elements[idx]) {
            ++idx;
        }

        if (idx < static_cast<int>(node->elements.size()) && node->elements[idx] == targetValue) {
            if (node->isLeafNode) {
                node->elements.erase(node->elements.begin() + idx);
            } else if (static_cast<int>(node->children[idx]->elements.size()) >= minDeg) {
                int predecessorVal = getMaximumKey(node->children[idx]);
                node->elements[idx] = predecessorVal;
                removeValue(node->children[idx], predecessorVal);
            } else if (static_cast<int>(node->children[idx + 1]->elements.size()) >= minDeg) {
                int successorVal = getMinimumKey(node->children[idx + 1]);
                node->elements[idx] = successorVal;
                removeValue(node->children[idx + 1], successorVal);
            } else {
                mergeNodes(node, idx);
                removeValue(node->children[idx], targetValue);
            }
            return;
        }

        if (node->isLeafNode) return;

        bool isLastChild = (idx == static_cast<int>(node->elements.size()));
        fulfillMinimumKeys(node, idx);

        if (isLastChild && idx > static_cast<int>(node->elements.size())) {
            removeValue(node->children[idx - 1], targetValue);
        } else {
            removeValue(node->children[idx], targetValue);
        }
    }

    void inorderTraversal(BTreeNode* node) const {
        if (!node) return;

        for (size_t idx = 0; idx < node->elements.size(); ++idx) {
            if (!node->isLeafNode) {
                inorderTraversal(node->children[idx]);
            }
            std::cout << node->elements[idx] << " ";
        }

        if (!node->isLeafNode) {
            inorderTraversal(node->children.back());
        }
    }
};

int main() {
    std::cout << "B-Tree Index Implementation\n";
    std::cout << "Patel Jash | 24bcs10632\n\n";

    BTreeIndex bTree(2);

    const std::vector<int> dataValues = {
        15, 25, 5, 35, 45,
        10, 20, 30, 40, 50,
        60, 12, 18, 28
    };

    std::cout << "Inserted Keys: ";
    for (int val : dataValues) {
        std::cout << val << " ";
        bTree.insertKey(val);
    }
    std::cout << "\n\n";

    std::cout << "Tree Structure By Levels:\n";
    bTree.displayLevels();

    std::cout << "\nSorted Keys: ";
    bTree.displaySorted();

    std::cout << "Search 28 = " << (bTree.findKey(28) ? "Found" : "Not Found") << "\n";
    std::cout << "Search 99 = " << (bTree.findKey(99) ? "Found" : "Not Found") << "\n";

    std::cout << "\nRemoving 10 and 35...\n";
    bTree.deleteKey(10);
    bTree.deleteKey(35);

    bTree.displayLevels();

    std::cout << "Sorted Keys After Deletion: ";
    bTree.displaySorted();

    return 0;
}