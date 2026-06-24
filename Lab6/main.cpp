#include <iostream>
#include <vector>
#include <queue>

using namespace std;

struct BTreeNode {
    bool isLeaf;
    vector<int> keys;
    vector<BTreeNode*> children;

    BTreeNode(bool leaf) {
        this->isLeaf = leaf;
    }
};

class BTree {
private:
    BTreeNode* root;
    int t; 

    void splitNode(BTreeNode* parent, int i, BTreeNode* fullChild) {
        BTreeNode* newChild = new BTreeNode(fullChild->isLeaf);
        int mid = t - 1;

        for (int j = 0; j < mid; j++) {
            newChild->keys.push_back(fullChild->keys[j + t]);
        }

        if (!fullChild->isLeaf) {
            for (int j = 0; j < t; j++) {
                newChild->children.push_back(fullChild->children[j + t]);
            }
        }

        int medianKey = fullChild->keys[mid];

        fullChild->keys.resize(mid);
        if (!fullChild->isLeaf) {
            fullChild->children.resize(mid + 1);
        }

        parent->children.insert(parent->children.begin() + i + 1, newChild);
        parent->keys.insert(parent->keys.begin() + i, medianKey);
    }

    void insertIntoNonFull(BTreeNode* node, int key) {
        int i = node->keys.size() - 1;

        if (node->isLeaf) {
            node->keys.push_back(0); 
            while (i >= 0 && node->keys[i] > key) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }
            node->keys[i + 1] = key;
        } else {
            while (i >= 0 && node->keys[i] > key) {
                i--;
            }
            i++; 

            if (node->children[i]->keys.size() == (size_t)(2 * t - 1)) {
                splitNode(node, i, node->children[i]);
                if (key > node->keys[i]) {
                    i++;
                }
            }
            insertIntoNonFull(node->children[i], key);
        }
    }

    int getPredecessor(BTreeNode* node, int idx) {
        BTreeNode* current = node->children[idx];
        while (!current->isLeaf) {
            current = current->children.back();
        }
        return current->keys.back();
    }

    int getSuccessor(BTreeNode* node, int idx) {
        BTreeNode* current = node->children[idx + 1];
        while (!current->isLeaf) {
            current = current->children.front();
        }
        return current->keys.front();
    }

    void mergeChildren(BTreeNode* node, int idx) {
        BTreeNode* leftChild = node->children[idx];
        BTreeNode* rightChild = node->children[idx + 1];

        leftChild->keys.push_back(node->keys[idx]);

        for (int key : rightChild->keys) {
            leftChild->keys.push_back(key);
        }
        if (!leftChild->isLeaf) {
            for (BTreeNode* child : rightChild->children) {
                leftChild->children.push_back(child);
            }
        }

        node->keys.erase(node->keys.begin() + idx);
        node->children.erase(node->children.begin() + idx + 1);

        delete rightChild;
    }

    void borrowFromLeft(BTreeNode* node, int idx) {
        BTreeNode* child = node->children[idx];
        BTreeNode* sibling = node->children[idx - 1];

        child->keys.insert(child->keys.begin(), node->keys[idx - 1]);
        if (!child->isLeaf) {
            child->children.insert(child->children.begin(), sibling->children.back());
            sibling->children.pop_back();
        }
        node->keys[idx - 1] = sibling->keys.back();
        sibling->keys.pop_back();
    }

    void borrowFromRight(BTreeNode* node, int idx) {
        BTreeNode* child = node->children[idx];
        BTreeNode* sibling = node->children[idx + 1];

        child->keys.push_back(node->keys[idx]);
        if (!child->isLeaf) {
            child->children.push_back(sibling->children.front());
            sibling->children.erase(sibling->children.begin());
        }
        node->keys[idx] = sibling->keys.front();
        sibling->keys.erase(sibling->keys.begin());
    }

    void repairChild(BTreeNode* node, int idx) {
        if (idx > 0 && node->children[idx - 1]->keys.size() >= (size_t)t) {
            borrowFromLeft(node, idx);
        } else if (idx < (int)node->keys.size() && node->children[idx + 1]->keys.size() >= (size_t)t) {
            borrowFromRight(node, idx);
        } else {
            if (idx < (int)node->keys.size()) {
                mergeChildren(node, idx);
            } else {
                mergeChildren(node, idx - 1);
            }
        }
    }

    void deleteFromInternal(BTreeNode* node, int idx) {
        int key = node->keys[idx];

        if (node->children[idx]->keys.size() >= (size_t)t) {
            int pred = getPredecessor(node, idx);
            node->keys[idx] = pred;
            deleteKey(node->children[idx], pred);
        } else if (node->children[idx + 1]->keys.size() >= (size_t)t) {
            int succ = getSuccessor(node, idx);
            node->keys[idx] = succ;
            deleteKey(node->children[idx + 1], succ);
        } else {
            mergeChildren(node, idx);
            deleteKey(node->children[idx], key);
        }
    }

    void deleteKey(BTreeNode* node, int key) {
        int idx = 0;
        while (idx < (int)node->keys.size() && node->keys[idx] < key) {
            idx++;
        }

        if (idx < (int)node->keys.size() && node->keys[idx] == key) {
            if (node->isLeaf) {
                node->keys.erase(node->keys.begin() + idx);
            } else {
                deleteFromInternal(node, idx);
            }
        } else {
            if (node->isLeaf) {
                return; 
            }
            bool isLast = (idx == (int)node->keys.size());

            if (node->children[idx]->keys.size() < (size_t)t) {
                repairChild(node, idx);
            }

            if (isLast && idx > (int)node->keys.size()) {
                deleteKey(node->children[idx - 1], key);
            } else {
                deleteKey(node->children[idx], key);
            }
        }
    }

    bool searchKey(BTreeNode* node, int key) {
        if (!node) return false;
        int i = 0;
        while (i < (int)node->keys.size() && key > node->keys[i]) {
            i++;
        }
        if (i < (int)node->keys.size() && node->keys[i] == key) {
            return true;
        }
        if (node->isLeaf) {
            return false;
        }
        return searchKey(node->children[i], key);
    }

    void inorderTraversal(BTreeNode* node) {
        if (!node) return;
        int i;
        for (i = 0; i < (int)node->keys.size(); i++) {
            if (!node->isLeaf) {
                inorderTraversal(node->children[i]);
            }
            cout << node->keys[i] << " ";
        }
        if (!node->isLeaf) {
            inorderTraversal(node->children[i]);
        }
    }

    void clearTree(BTreeNode* node) {
        if (!node) return;
        if (!node->isLeaf) {
            for (auto child : node->children) {
                clearTree(child);
            }
        }
        delete node;
    }

public:
    BTree(int minDegree) {
        t = minDegree;
        root = new BTreeNode(true);
    }

    ~BTree() {
        clearTree(root);
    }

    void insert(int key) {
        if (root->keys.size() == (size_t)(2 * t - 1)) {
            BTreeNode* newRoot = new BTreeNode(false);
            newRoot->children.push_back(root);
            splitNode(newRoot, 0, root);
            root = newRoot;
        }
        insertIntoNonFull(root, key);
    }

    void remove(int key) {
        if (root->keys.empty()) return;

        deleteKey(root, key);

        if (root->keys.empty()) {
            BTreeNode* obsoleteRoot = root;
            if (root->isLeaf) {
                root = new BTreeNode(true);
            } else {
                root = root->children[0];
            }
            delete obsoleteRoot;
        }
    }

    bool search(int key) {
        return searchKey(root, key);
    }

    void printInorder() {
        inorderTraversal(root);
        cout << "\n";
    }

    void printLevelOrder() {
        if (root->keys.empty()) {
            cout << "(Tree is empty)\n";
            return;
        }

        queue<pair<BTreeNode*, int>> q;
        q.push({root, 0});
        int prevLevel = -1;

        while (!q.empty()) {
            auto [node, level] = q.front();
            q.pop();

            if (level != prevLevel) {
                if (prevLevel != -1) cout << "\n";
                cout << "L" << level << ":  ";
                prevLevel = level;
            }

            cout << "[";
            for (size_t i = 0; i < node->keys.size(); i++) {
                cout << node->keys[i] << (i + 1 == node->keys.size() ? "" : ", ");
            }
            cout << "]  ";

            if (!node->isLeaf) {
                for (auto child : node->children) {
                    q.push({child, level + 1});
                }
            }
        }
        cout << "\n";
    }
};

int main() {
    int degree;
    cout << "Enter the minimum degree of the B-Tree (t >= 2): ";
    cin >> degree;

    if (degree < 2) {
        cout << "Error: The minimum degree must be at least 2.\n";
        return 1;
    }

    BTree bTree(degree);
    int choice, val;
    bool running = true;

    while (running) {
        cout << "\n=== B-Tree Operations ===\n"
             << "1. Insert Key\n"
             << "2. Delete Key\n"
             << "3. Search Key\n"
             << "4. Print Inorder\n"
             << "5. Print Level-Order\n"
             << "6. Exit\n"
             << "Select an option: ";
        cin >> choice;

        switch (choice) {
            case 1:
                cout << "Enter value to insert: ";
                cin >> val;
                bTree.insert(val);
                break;
            case 2:
                cout << "Enter value to delete: ";
                cin >> val;
                bTree.remove(val);
                break;
            case 3:
                cout << "Enter value to search: ";
                cin >> val;
                if (bTree.search(val)) {
                    cout << "Result: " << val << " found in the tree.\n";
                } else {
                    cout << "Result: " << val << " not found.\n";
                }
                break;
            case 4:
                cout << "Inorder Traversal: ";
                bTree.printInorder();
                break;
            case 5:
                cout << "Level-Order View:\n";
                bTree.printLevelOrder();
                break;
            case 6:
                cout << "Terminating program...\n";
                running = false;
                break;
            default:
                cout << "Invalid selection. Please enter a number from 1 to 6.\n";
                break;
        }
    }

    return 0;
}