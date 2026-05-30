#include <iostream>
#include <vector>
#include <queue>

using namespace std;

class Node {
public:
    bool leaf;
    vector<int> values;
    vector<Node*> ptrs;

    Node(bool isLeaf) {
        leaf = isLeaf;
    }
};

class BTree {
private:
    Node* root;
    int degree;

    // ================= SPLIT CHILD =================
    void splitChild(Node* parent, int index) {
        Node* fullNode = parent->ptrs[index];
        Node* rightNode = new Node(fullNode->leaf);

        int middle = degree - 1;

        // Move keys
        for (int i = degree; i < (int)fullNode->values.size(); i++) {
            rightNode->values.push_back(fullNode->values[i]);
        }

        // Move children if internal node
        if (!fullNode->leaf) {
            for (int i = degree; i < (int)fullNode->ptrs.size(); i++) {
                rightNode->ptrs.push_back(fullNode->ptrs[i]);
            }
        }

        int promotedKey = fullNode->values[middle];

        // Resize left node
        fullNode->values.resize(middle);

        if (!fullNode->leaf) {
            fullNode->ptrs.resize(degree);
        }

        parent->values.insert(parent->values.begin() + index, promotedKey);
        parent->ptrs.insert(parent->ptrs.begin() + index + 1, rightNode);
    }

    // ================= INSERT =================
    void insertNonFull(Node* current, int key) {
        int pos = current->values.size() - 1;

        if (current->leaf) {
            current->values.push_back(0);

            while (pos >= 0 && current->values[pos] > key) {
                current->values[pos + 1] = current->values[pos];
                pos--;
            }

            current->values[pos + 1] = key;
        }
        else {
            while (pos >= 0 && key < current->values[pos]) {
                pos--;
            }

            pos++;

            if (current->ptrs[pos]->values.size() == 2 * degree - 1) {
                splitChild(current, pos);

                if (key > current->values[pos]) {
                    pos++;
                }
            }

            insertNonFull(current->ptrs[pos], key);
        }
    }

    // ================= SEARCH =================
    bool find(Node* node, int key) {
        if (!node) return false;

        int i = 0;

        while (i < (int)node->values.size() && key > node->values[i]) {
            i++;
        }

        if (i < (int)node->values.size() && node->values[i] == key) {
            return true;
        }

        if (node->leaf) {
            return false;
        }

        return find(node->ptrs[i], key);
    }

    // ================= TRAVERSAL =================
    void inorder(Node* node) {
        if (!node) return;

        int i;

        for (i = 0; i < (int)node->values.size(); i++) {

            if (!node->leaf) {
                inorder(node->ptrs[i]);
            }

            cout << node->values[i] << " ";
        }

        if (!node->leaf) {
            inorder(node->ptrs[i]);
        }
    }

    // ================= PREDECESSOR =================
    int getPredecessor(Node* node) {
        Node* cur = node;

        while (!cur->leaf) {
            cur = cur->ptrs.back();
        }

        return cur->values.back();
    }

    // ================= SUCCESSOR =================
    int getSuccessor(Node* node) {
        Node* cur = node;

        while (!cur->leaf) {
            cur = cur->ptrs.front();
        }

        return cur->values.front();
    }

    // ================= MERGE =================
    void mergeNodes(Node* parent, int idx) {

        Node* left = parent->ptrs[idx];
        Node* right = parent->ptrs[idx + 1];

        left->values.push_back(parent->values[idx]);

        for (int x : right->values) {
            left->values.push_back(x);
        }

        if (!left->leaf) {
            for (Node* child : right->ptrs) {
                left->ptrs.push_back(child);
            }
        }

        parent->values.erase(parent->values.begin() + idx);
        parent->ptrs.erase(parent->ptrs.begin() + idx + 1);

        delete right;
    }

    // ================= BORROW LEFT =================
    void takeFromLeft(Node* parent, int idx) {

        Node* child = parent->ptrs[idx];
        Node* sibling = parent->ptrs[idx - 1];

        child->values.insert(child->values.begin(), parent->values[idx - 1]);

        if (!child->leaf) {
            child->ptrs.insert(child->ptrs.begin(), sibling->ptrs.back());
            sibling->ptrs.pop_back();
        }

        parent->values[idx - 1] = sibling->values.back();
        sibling->values.pop_back();
    }

    // ================= BORROW RIGHT =================
    void takeFromRight(Node* parent, int idx) {

        Node* child = parent->ptrs[idx];
        Node* sibling = parent->ptrs[idx + 1];

        child->values.push_back(parent->values[idx]);

        if (!child->leaf) {
            child->ptrs.push_back(sibling->ptrs.front());
            sibling->ptrs.erase(sibling->ptrs.begin());
        }

        parent->values[idx] = sibling->values.front();
        sibling->values.erase(sibling->values.begin());
    }

    // ================= FIX CHILD =================
    void balance(Node* node, int idx) {

        if (idx > 0 && node->ptrs[idx - 1]->values.size() >= degree) {
            takeFromLeft(node, idx);
        }
        else if (idx < (int)node->values.size() &&
                 node->ptrs[idx + 1]->values.size() >= degree) {

            takeFromRight(node, idx);
        }
        else {

            if (idx < (int)node->values.size()) {
                mergeNodes(node, idx);
            }
            else {
                mergeNodes(node, idx - 1);
            }
        }
    }

    // ================= DELETE =================
    void erase(Node* node, int key) {

        int idx = 0;

        while (idx < (int)node->values.size() &&
               node->values[idx] < key) {
            idx++;
        }

        // Key found
        if (idx < (int)node->values.size() &&
            node->values[idx] == key) {

            // Leaf node
            if (node->leaf) {
                node->values.erase(node->values.begin() + idx);
            }
            else {

                Node* leftChild = node->ptrs[idx];
                Node* rightChild = node->ptrs[idx + 1];

                if (leftChild->values.size() >= degree) {

                    int pred = getPredecessor(leftChild);
                    node->values[idx] = pred;
                    erase(leftChild, pred);
                }
                else if (rightChild->values.size() >= degree) {

                    int succ = getSuccessor(rightChild);
                    node->values[idx] = succ;
                    erase(rightChild, succ);
                }
                else {

                    mergeNodes(node, idx);
                    erase(leftChild, key);
                }
            }
        }
        else {

            if (node->leaf) {
                return;
            }

            bool lastChild = (idx == node->values.size());

            if (node->ptrs[idx]->values.size() < degree) {
                balance(node, idx);
            }

            if (lastChild && idx > node->values.size()) {
                erase(node->ptrs[idx - 1], key);
            }
            else {
                erase(node->ptrs[idx], key);
            }
        }
    }

    // ================= CLEANUP =================
    void destroy(Node* node) {

        if (!node) return;

        if (!node->leaf) {
            for (Node* child : node->ptrs) {
                destroy(child);
            }
        }

        delete node;
    }

public:

    BTree(int t) {
        degree = t;
        root = new Node(true);
    }

    ~BTree() {
        destroy(root);
    }

    // ================= PUBLIC INSERT =================
    void insert(int key) {

        if (root->values.size() == 2 * degree - 1) {

            Node* newRoot = new Node(false);

            newRoot->ptrs.push_back(root);

            splitChild(newRoot, 0);

            root = newRoot;
        }

        insertNonFull(root, key);
    }

    // ================= PUBLIC DELETE =================
    void remove(int key) {

        if (!root) return;

        erase(root, key);

        if (root->values.empty()) {

            Node* oldRoot = root;

            if (root->leaf) {
                root = new Node(true);
            }
            else {
                root = root->ptrs[0];
            }

            delete oldRoot;
        }
    }

    // ================= PUBLIC SEARCH =================
    bool search(int key) {
        return find(root, key);
    }

    // ================= INORDER =================
    void printInorder() {
        inorder(root);
        cout << "\n";
    }

    // ================= LEVEL ORDER =================
    void printLevels() {

        if (root->values.empty()) {
            cout << "Tree is empty\n";
            return;
        }

        queue<pair<Node*, int>> q;

        q.push({root, 0});

        int currentLevel = -1;

        while (!q.empty()) {

            auto front = q.front();
            q.pop();

            Node* node = front.first;
            int level = front.second;

            if (level != currentLevel) {

                currentLevel = level;

                cout << "\nLevel " << level << ": ";
            }

            cout << "[";

            for (int i = 0; i < (int)node->values.size(); i++) {

                cout << node->values[i];

                if (i + 1 != node->values.size()) {
                    cout << ", ";
                }
            }

            cout << "] ";

            if (!node->leaf) {

                for (Node* child : node->ptrs) {
                    q.push({child, level + 1});
                }
            }
        }

        cout << "\n";
    }
};

int main() {

    int t;

    cout << "Enter minimum degree: ";
    cin >> t;

    if (t < 2) {
        cout << "Degree must be at least 2\n";
        return 0;
    }

    BTree tree(t);

    int choice, value;

    while (true) {

        cout << "\n========== B TREE ==========\n";
        cout << "1. Insert\n";
        cout << "2. Delete\n";
        cout << "3. Search\n";
        cout << "4. Inorder Traversal\n";
        cout << "5. Level Order View\n";
        cout << "6. Exit\n";
        cout << "Choice: ";

        cin >> choice;

        switch (choice) {

        case 1:
            cout << "Enter value: ";
            cin >> value;
            tree.insert(value);
            break;

        case 2:
            cout << "Enter value: ";
            cin >> value;
            tree.remove(value);
            break;

        case 3:
            cout << "Enter value: ";
            cin >> value;

            if (tree.search(value)) {
                cout << "Key found\n";
            }
            else {
                cout << "Key not found\n";
            }

            break;

        case 4:
            cout << "Inorder: ";
            tree.printInorder();
            break;

        case 5:
            tree.printLevels();
            break;

        case 6:
            cout << "Program ended\n";
            return 0;

        default:
            cout << "Invalid option\n";
        }
    }

    return 0;
}