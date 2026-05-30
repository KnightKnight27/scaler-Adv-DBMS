#include <iostream>
#include <vector>
#include <queue>

using namespace std;

// Node component configuration for the B-Tree structure
struct CustomBTreeNode {
    bool leafStatus;
    vector<int> itemKeys;
    vector<CustomBTreeNode*> childLinks;

    CustomBTreeNode(bool checkLeaf) {
        this->leafStatus = checkLeaf;
    }
};

class CustomBTree {
private:
    CustomBTreeNode* treeRoot;
    int minDeg; // The fundamental degree determining branch calculations (t)

    // Divides a saturated child node when traversing top-down
    void executeNodeSplit(CustomBTreeNode* parentNode, int childIndex, CustomBTreeNode* saturatedNode) {
        CustomBTreeNode* freshSiblingNode = new CustomBTreeNode(saturatedNode->leafStatus);
        int middleIndex = minDeg - 1;

        // Populate the fresh sibling node using the second half of the keys
        for (int idx = 0; idx < middleIndex; idx++) {
            freshSiblingNode->itemKeys.push_back(saturatedNode->itemKeys[idx + minDeg]);
        }

        // Migrate corresponding children arrays if this isn't a leaf component
        if (!saturatedNode->leafStatus) {
            for (int idx = 0; idx < minDeg; idx++) {
                freshSiblingNode->childLinks.push_back(saturatedNode->childLinks[idx + minDeg]);
            }
        }

        int elevatedKey = saturatedNode->itemKeys[middleIndex];

        // Trim the original saturated child node down to size
        saturatedNode->itemKeys.resize(middleIndex);
        if (!saturatedNode->leafStatus) {
            saturatedNode->childLinks.resize(middleIndex + 1);
        }

        // Stitch the new sibling element and middle key back into the tracking parent
        parentNode->childLinks.insert(parentNode->childLinks.begin() + childIndex + 1, freshSiblingNode);
        parentNode->itemKeys.insert(parentNode->itemKeys.begin() + childIndex, elevatedKey);
    }

    // Handles element insertion on nodes pre-validated to have extra capacity
    void insertIntoAvailableSpace(CustomBTreeNode* currentNode, int targetKey) {
        int pointerIdx = currentNode->itemKeys.size() - 1;

        if (currentNode->leafStatus) {
            // Expand space and perform inline insertion sort shifts
            currentNode->itemKeys.push_back(0); 
            while (pointerIdx >= 0 && currentNode->itemKeys[pointerIdx] > targetKey) {
                currentNode->itemKeys[pointerIdx + 1] = currentNode->itemKeys[pointerIdx];
                pointerIdx--;
            }
            currentNode->itemKeys[pointerIdx + 1] = targetKey;
        } else {
            // Find appropriate child destination branch
            while (pointerIdx >= 0 && currentNode->itemKeys[pointerIdx] > targetKey) {
                pointerIdx--;
            }
            pointerIdx++; 

            // Intercept and split the child mid-descent if it is full
            if (currentNode->childLinks[pointerIdx]->itemKeys.size() == (size_t)(2 * minDeg - 1)) {
                executeNodeSplit(currentNode, pointerIdx, currentNode->childLinks[pointerIdx]);
                if (targetKey > currentNode->itemKeys[pointerIdx]) {
                    pointerIdx++;
                }
            }
            insertIntoAvailableSpace(currentNode->childLinks[pointerIdx], targetKey);
        }
    }

    // Resolves left-side boundary search for internal node swaps
    int locatePredecessorValue(CustomBTreeNode* startingNode, int structuralPos) {
        CustomBTreeNode* walker = startingNode->childLinks[structuralPos];
        while (!walker->leafStatus) {
            walker = walker->childLinks.back();
        }
        return walker->itemKeys.back();
    }

    // Resolves right-side boundary search for internal node swaps
    int locateSuccessorValue(CustomBTreeNode* startingNode, int structuralPos) {
        CustomBTreeNode* walker = startingNode->childLinks[structuralPos + 1];
        while (!walker->leafStatus) {
            walker = walker->childLinks.front();
        }
        return walker->itemKeys.front();
    }

    // Combines separate children arrays across a specific slot index
    void consolidateChildren(CustomBTreeNode* hostNode, int structuralPos) {
        CustomBTreeNode* leftTarget = hostNode->childLinks[structuralPos];
        CustomBTreeNode* rightTarget = hostNode->childLinks[structuralPos + 1];

        leftTarget->itemKeys.push_back(hostNode->itemKeys[structuralPos]);

        for (int val : rightTarget->itemKeys) {
            leftTarget->itemKeys.push_back(val);
        }
        if (!leftTarget->leafStatus) {
            for (CustomBTreeNode* pointer : rightTarget->childLinks) {
                leftTarget->childLinks.push_back(pointer);
            }
        }

        hostNode->itemKeys.erase(hostNode->itemKeys.begin() + structuralPos);
        hostNode->childLinks.erase(hostNode->childLinks.begin() + structuralPos + 1);

        delete rightTarget;
    }

    // Shifts an extra element from a left-hand neighbor to maintain balance
    void pullFromLeftNeighbor(CustomBTreeNode* hostNode, int structuralPos) {
        CustomBTreeNode* targetedChild = hostNode->childLinks[structuralPos];
        CustomBTreeNode* leftSibling = hostNode->childLinks[structuralPos - 1];

        targetedChild->itemKeys.insert(targetedChild->itemKeys.begin(), hostNode->itemKeys[structuralPos - 1]);
        if (!targetedChild->leafStatus) {
            targetedChild->childLinks.insert(targetedChild->childLinks.begin(), leftSibling->childLinks.back());
            leftSibling->childLinks.pop_back();
        }
        hostNode->itemKeys[structuralPos - 1] = leftSibling->itemKeys.back();
        leftSibling->itemKeys.pop_back();
    }

    // Shifts an extra element from a right-hand neighbor to maintain balance
    void pullFromRightNeighbor(CustomBTreeNode* hostNode, int structuralPos) {
        CustomBTreeNode* targetedChild = hostNode->childLinks[structuralPos];
        CustomBTreeNode* rightSibling = hostNode->childLinks[structuralPos + 1];

        targetedChild->itemKeys.push_back(hostNode->itemKeys[structuralPos]);
        if (!targetedChild->leafStatus) {
            targetedChild->childLinks.push_back(rightSibling->childLinks.front());
            rightSibling->childLinks.erase(rightSibling->childLinks.begin());
        }
        hostNode->itemKeys[structuralPos] = rightSibling->itemKeys.front();
        rightSibling->childLinks.erase(rightSibling->childLinks.begin());
    }

    // Re-establishes valid item weights on child nodes that fall under limit thresholds
    void reinforceChildNode(CustomBTreeNode* hostNode, int structuralPos) {
        if (structuralPos > 0 && hostNode->childLinks[structuralPos - 1]->itemKeys.size() >= (size_t)minDeg) {
            pullFromLeftNeighbor(hostNode, structuralPos);
        } else if (structuralPos < (int)hostNode->itemKeys.size() && hostNode->childLinks[structuralPos + 1]->itemKeys.size() >= (size_t)minDeg) {
            pullFromRightNeighbor(hostNode, structuralPos);
        } else {
            if (structuralPos < (int)hostNode->itemKeys.size()) {
                consolidateChildren(hostNode, structuralPos);
            } else {
                consolidateChildren(hostNode, structuralPos - 1);
            }
        }
    }

    // Manages structural deletion when the requested value sits inside an interior layer
    void executeInternalErasure(CustomBTreeNode* hostNode, int structuralPos) {
        int targetedVal = hostNode->itemKeys[structuralPos];

        if (hostNode->childLinks[structuralPos]->itemKeys.size() >= (size_t)minDeg) {
            int alternativePred = locatePredecessorValue(hostNode, structuralPos);
            hostNode->itemKeys[structuralPos] = alternativePred;
            removeTargetKey(hostNode->childLinks[structuralPos], alternativePred);
        } else if (hostNode->childLinks[structuralPos + 1]->itemKeys.size() >= (size_t)minDeg) {
            int alternativeSucc = locateSuccessorValue(hostNode, structuralPos);
            hostNode->itemKeys[structuralPos] = alternativeSucc;
            removeTargetKey(hostNode->childLinks[structuralPos + 1], alternativeSucc);
        } else {
            consolidateChildren(hostNode, structuralPos);
            removeTargetKey(hostNode->childLinks[structuralPos], targetedVal);
        }
    }

    // Primary internal recursive workflow for scrubbing structural values
    void removeTargetKey(CustomBTreeNode* hostNode, int targetKey) {
        int activeIdx = 0;
        while (activeIdx < (int)hostNode->itemKeys.size() && hostNode->itemKeys[activeIdx] < targetKey) {
            activeIdx++;
        }

        if (activeIdx < (int)hostNode->itemKeys.size() && hostNode->itemKeys[activeIdx] == targetKey) {
            if (hostNode->leafStatus) {
                hostNode->itemKeys.erase(hostNode->itemKeys.begin() + activeIdx);
            } else {
                executeInternalErasure(hostNode, activeIdx);
            }
        } else {
            if (hostNode->leafStatus) {
                return; 
            }
            bool edgeStatus = (activeIdx == (int)hostNode->itemKeys.size());

            if (hostNode->childLinks[activeIdx]->itemKeys.size() < (size_t)minDeg) {
                reinforceChildNode(hostNode, activeIdx);
            }

            if (edgeStatus && activeIdx > (int)hostNode->itemKeys.size()) {
                removeTargetKey(hostNode->childLinks[activeIdx - 1], targetKey);
            } else {
                removeTargetKey(hostNode->childLinks[activeIdx], targetKey);
            }
        }
    }

    // Scans internal nodes to verify existence of a requested target
    bool locateTargetValue(CustomBTreeNode* startingNode, int targetKey) {
        if (!startingNode) return false;
        int activeIdx = 0;
        while (activeIdx < (int)startingNode->itemKeys.size() && targetKey > startingNode->itemKeys[activeIdx]) {
            activeIdx++;
        }
        if (activeIdx < (int)startingNode->itemKeys.size() && startingNode->itemKeys[activeIdx] == targetKey) {
            return true;
        }
        if (startingNode->leafStatus) {
            return false;
        }
        return locateTargetValue(startingNode->childLinks[activeIdx], targetKey);
    }

    // Performs standard ordered key sequence tracking printouts
    void runInorderPrint(CustomBTreeNode* startingNode) {
        if (!startingNode) return;
        int activeIdx;
        for (activeIdx = 0; activeIdx < (int)startingNode->itemKeys.size(); activeIdx++) {
            if (!startingNode->leafStatus) {
                runInorderPrint(startingNode->childLinks[activeIdx]);
            }
            cout << startingNode->itemKeys[activeIdx] << " ";
        }
        if (!startingNode->leafStatus) {
            runInorderPrint(startingNode->childLinks[activeIdx]);
        }
    }

    // Dynamic garbage collection routine for structure clearance
    void deallocateMemory(CustomBTreeNode* targetedNode) {
        if (!targetedNode) return;
        if (!targetedNode->leafStatus) {
            for (auto leafPointer : targetedNode->childLinks) {
                deallocateMemory(leafPointer);
            }
        }
        delete targetedNode;
    }

public:
    CustomBTree(int setupDegree) {
        minDeg = setupDegree;
        treeRoot = new CustomBTreeNode(true);
    }

    ~CustomBTree() {
        deallocateMemory(treeRoot);
    }

    // Root-level handling interface for adding fresh keys
    void addKey(int entryValue) {
        if (treeRoot->itemKeys.size() == (size_t)(2 * minDeg - 1)) {
            CustomBTreeNode* freshRoot = new CustomBTreeNode(false);
            freshRoot->childLinks.push_back(treeRoot);
            executeNodeSplit(freshRoot, 0, treeRoot);
            treeRoot = freshRoot;
        }
        insertIntoAvailableSpace(treeRoot, entryValue);
    }

    // Public removal management method for target values
    void discardKey(int erasureValue) {
        if (treeRoot->itemKeys.empty()) return;

        removeTargetKey(treeRoot, erasureValue);

        if (treeRoot->itemKeys.empty()) {
            CustomBTreeNode* residualRoot = treeRoot;
            if (treeRoot->leafStatus) {
                treeRoot = new CustomBTreeNode(true);
            } else {
                treeRoot = treeRoot->childLinks[0];
            }
            delete residualRoot;
        }
    }

    // Public searching pathway wrapper
    bool findKey(int lookupValue) {
        return locateTargetValue(treeRoot, lookupValue);
    }

    // Outputs simple horizontal text mapping sequences
    void displaySortedOrder() {
        runInorderPrint(treeRoot);
        cout << "\n";
    }

    // Layer-by-layer tree map display generated via BFS processing queues
    void displayLayerLayout() {
        if (treeRoot->itemKeys.empty()) {
            cout << "(Tree contains no data elements)\n";
            return;
        }

        queue<pair<CustomBTreeNode*, int>> processingQueue;
        processingQueue.push({treeRoot, 0});
        int trackerLevel = -1;

        while (!processingQueue.empty()) {
            auto [activeNode, structuralLevel] = processingQueue.front();
            processingQueue.pop();

            if (structuralLevel != trackerLevel) {
                if (trackerLevel != -1) cout << "\n";
                cout << "Level " << structuralLevel << ":  ";
                trackerLevel = structuralLevel;
            }

            cout << "[";
            for (size_t loopIdx = 0; loopIdx < activeNode->itemKeys.size(); loopIdx++) {
                cout << activeNode->itemKeys[loopIdx] << (loopIdx + 1 == activeNode->itemKeys.size() ? "" : ", ");
            }
            cout << "]  ";

            if (!activeNode->leafStatus) {
                for (auto downstreamChild : activeNode->childLinks) {
                    processingQueue.push({downstreamChild, structuralLevel + 1});
                }
            }
        }
        cout << "\n";
    }
};

int main() {
    int requestedDegree;
    cout << "Establish the B-Tree minimum degree capacity (t >= 2): ";
    cin >> requestedDegree;

    if (requestedDegree < 2) {
        cout << "Critical Error: Configuration degree must fall at or above 2.\n";
        return 1;
    }

    CustomBTree activeTree(requestedDegree);
    int selectedOption, userValue;
    bool systemLoop = true;

    while (systemLoop) {
        cout << "\n===============================\n"
             << "   B-TREE CONTROL PANEL\n"
             << "===============================\n"
             << "1) Add Value\n"
             << "2) Remove Value\n"
             << "3) Query Value\n"
             << "4) Render Sorted Sequence (Inorder)\n"
             << "5) Visual Map Generation (Level-Order)\n"
             << "6) Terminate Session\n"
             << "Provide instruction index: ";
        cin >> selectedOption;

        switch (selectedOption) {
            case 1:
                cout << "Value to insert: ";
                cin >> userValue;
                activeTree.addKey(userValue);
                break;
            case 2:
                cout << "Value to drop: ";
                cin >> userValue;
                activeTree.discardKey(userValue);
                break;
            case 3:
                cout << "Target search query value: ";
                cin >> userValue;
                if (activeTree.findKey(userValue)) {
                    cout << "Status Report: Elements matching (" << userValue << ") are present.\n";
                } else {
                    cout << "Status Report: No structural match for (" << userValue << ") located.\n";
                }
                break;
            case 4:
                cout << "Linear Sorted Traversal: ";
                activeTree.displaySortedOrder();
                break;
            case 5:
                cout << "Current Multi-Level Structural Overview:\n";
                activeTree.displayLayerLayout();
                break;
            case 6:
                cout << "Exiting control program and cleaning resources...\n";
                systemLoop = false;
                break;
            default:
                cout << "Selection ignored. Enter values exclusively within ranges [1-6].\n";
                break;
        }
    }

    return 0;
}