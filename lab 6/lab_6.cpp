#include <iostream>
#include <vector>

using namespace std;

class BTreeNode {
public:
    vector<int> keys;
    vector<BTreeNode*> children;
    bool leaf;

    BTreeNode(bool isLeaf) {
        leaf = isLeaf;
    }
};

class BTree {
private:
    BTreeNode* root;
    int t;

    void splitChild(BTreeNode* parent, int index, BTreeNode* child) {
        BTreeNode* newNode = new BTreeNode(child->leaf);

        int median = child->keys[t - 1];

        for (int i = t; i < child->keys.size(); i++) {
            newNode->keys.push_back(child->keys[i]);
        }

        if (!child->leaf) {
            for (int i = t; i < child->children.size(); i++) {
                newNode->children.push_back(child->children[i]);
            }

            child->children.resize(t);
        }

        child->keys.resize(t - 1);

        parent->children.insert(
            parent->children.begin() + index + 1,
            newNode
        );

        parent->keys.insert(
            parent->keys.begin() + index,
            median
        );
    }

    void insertNonFull(BTreeNode* node, int key) {
        int i = node->keys.size() - 1;

        if (node->leaf) {
            node->keys.push_back(0);

            while (i >= 0 && key < node->keys[i]) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }

            node->keys[i + 1] = key;
        }
        else {
            while (i >= 0 && key < node->keys[i]) {
                i--;
            }

            i++;

            if (node->children[i]->keys.size() == 2 * t - 1) {
                splitChild(node, i, node->children[i]);

                if (key > node->keys[i]) {
                    i++;
                }
            }

            insertNonFull(node->children[i], key);
        }
    }

    bool searchNode(BTreeNode* node, int key) {
        if (node == nullptr)
            return false;

        int i = 0;

        while (i < node->keys.size() && key > node->keys[i]) {
            i++;
        }

        if (i < node->keys.size() && key == node->keys[i]) {
            return true;
        }

        if (node->leaf) {
            return false;
        }

        return searchNode(node->children[i], key);
    }

    void inorderTraversal(BTreeNode* node) {
        if (node == nullptr)
            return;

        int n = node->keys.size();

        for (int i = 0; i < n; i++) {
            if (!node->leaf) {
                inorderTraversal(node->children[i]);
            }

            cout << node->keys[i] << " ";
        }

        if (!node->leaf) {
            inorderTraversal(node->children[n]);
        }
    }

public:
    BTree(int degree) {
        root = nullptr;
        t = degree;
    }

    void insert(int key) {
        if (root == nullptr) {
            root = new BTreeNode(true);
            root->keys.push_back(key);
            return;
        }

        if (root->keys.size() == 2 * t - 1) {
            BTreeNode* newRoot = new BTreeNode(false);

            newRoot->children.push_back(root);

            splitChild(newRoot, 0, root);

            int i = 0;

            if (key > newRoot->keys[0]) {
                i++;
            }

            insertNonFull(newRoot->children[i], key);

            root = newRoot;
        }
        else {
            insertNonFull(root, key);
        }
    }

    bool search(int key) {
        return searchNode(root, key);
    }

    void display() {
        inorderTraversal(root);
        cout << endl;
    }
};

int main() {
    int degree;

    cout << "Enter minimum degree (t): ";
    cin >> degree;

    BTree tree(degree);

    int choice, key;

    do {
        cout << "\n===== B-Tree Menu =====\n";
        cout << "1. Insert\n";
        cout << "2. Search\n";
        cout << "3. Display (Inorder)\n";
        cout << "4. Quit\n";
        cout << "Enter choice: ";
        cin >> choice;

        switch (choice) {
        case 1:
            cout << "Enter key: ";
            cin >> key;
            tree.insert(key);
            break;

        case 2:
            cout << "Enter key to search: ";
            cin >> key;

            if (tree.search(key))
                cout << "Key is present in the tree.\n";
            else
                cout << "Key is not in the tree.\n";

            break;

        case 3:
            cout << "Inorder Traversal: ";
            tree.display();
            break;

        case 4:
            cout << "Exiting...\n";
            break;

        default:
            cout << "Invalid choice!\n";
        }

    } while (choice != 4);

    return 0;
}
