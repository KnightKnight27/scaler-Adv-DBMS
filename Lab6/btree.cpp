#include <iostream>
#include <vector>
using namespace std;

class Node {
public:
    bool leaf;
    vector<int> keys;
    vector<Node*> children;

    Node(bool isLeaf) {
        leaf = isLeaf;
    }
};

class BTree {
    Node* root;
    int degree;

    void splitNode(Node* parent, int index, Node* current) {
        Node* newNode = new Node(current->leaf);
        int mid = degree - 1;

        for (int i = mid + 1; i < current->keys.size(); i++)
            newNode->keys.push_back(current->keys[i]);

        if (!current->leaf) {
            for (int i = mid + 1; i < current->children.size(); i++)
                newNode->children.push_back(current->children[i]);
        }

        int promotedKey = current->keys[mid];

        current->keys.resize(mid);

        if (!current->leaf)
            current->children.resize(mid + 1);

        parent->children.insert(parent->children.begin() + index + 1, newNode);
        parent->keys.insert(parent->keys.begin() + index, promotedKey);
    }

    void insertHelper(Node* node, int value) {
        if (node->leaf) {
            node->keys.push_back(value);

            for (int i = node->keys.size() - 1; i > 0; i--) {
                if (node->keys[i] < node->keys[i - 1])
                    swap(node->keys[i], node->keys[i - 1]);
            }
        } else {
            int i = 0;

            while (i < node->keys.size() && value > node->keys[i])
                i++;

            if (node->children[i]->keys.size() == 2 * degree - 1) {
                splitNode(node, i, node->children[i]);

                if (value > node->keys[i])
                    i++;
            }

            insertHelper(node->children[i], value);
        }
    }

    bool searchHelper(Node* node, int value) {
        int i = 0;

        while (i < node->keys.size() && value > node->keys[i])
            i++;

        if (i < node->keys.size() && node->keys[i] == value)
            return true;

        if (node->leaf)
            return false;

        return searchHelper(node->children[i], value);
    }

    void traverseHelper(Node* node) {
        int i;

        for (i = 0; i < node->keys.size(); i++) {
            if (!node->leaf)
                traverseHelper(node->children[i]);

            cout << node->keys[i] << " ";
        }

        if (!node->leaf)
            traverseHelper(node->children[i]);
    }

public:
    BTree(int d) {
        degree = d;
        root = new Node(true);
    }

    void insert(int value) {
        if (root->keys.size() == 2 * degree - 1) {
            Node* newRoot = new Node(false);
            newRoot->children.push_back(root);

            splitNode(newRoot, 0, root);
            root = newRoot;
        }

        insertHelper(root, value);
    }

    bool search(int value) {
        return searchHelper(root, value);
    }

    void traverse() {
        traverseHelper(root);
        cout << endl;
    }
};

int main() {
    int degree;
    cout << "Enter minimum degree: ";
    cin >> degree;

    BTree tree(degree);

    int choice, value;

    do {
        cout << "\n1. Insert\n2. Search\n3. Traverse\n4. Exit\nEnter choice: ";
        cin >> choice;

        switch (choice) {
            case 1:
                cout << "Enter value to insert: ";
                cin >> value;
                tree.insert(value);
                break;

            case 2:
                cout << "Enter value to search: ";
                cin >> value;
                if (tree.search(value))
                    cout << "Value found.\n";
                else
                    cout << "Value not found.\n";
                break;

            case 3:
                cout << "B-Tree Traversal: ";
                tree.traverse();
                break;

            case 4:
                cout << "Exiting...\n";
                break;

            default:
                cout << "Invalid choice.\n";
        }

    } while (choice != 4);

    return 0;
}