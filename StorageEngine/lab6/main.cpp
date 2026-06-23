#include <iostream>
#include <vector>
using namespace std;

struct Node {
    bool isLeaf;
    vector<int> keys;
    vector<Node*> kids;

    Node(bool leaf) {
        isLeaf = leaf;
    }
};

class BTree {
    Node* root;
    int t;  // minimum degree

    void splitChild(Node* parent, int idx, Node* child) {
        Node* sibling = new Node(child->isLeaf);
        int mid = t - 1;

        for (int i = mid + 1; i < (int)child->keys.size(); i++) {
            sibling->keys.push_back(child->keys[i]);
        }

        if (!child->isLeaf) {
            for (int i = mid + 1; i < (int)child->kids.size(); i++) {
                sibling->kids.push_back(child->kids[i]);
            }
        }

        int median = child->keys[mid];

        child->keys.resize(mid);
        if (!child->isLeaf) {
            child->kids.resize(mid + 1);
        }

        parent->kids.insert(parent->kids.begin() + idx + 1, sibling);
        parent->keys.insert(parent->keys.begin() + idx, median);
    }

    void insertNonFull(Node* node, int key) {
        if (node->isLeaf) {
            int pos = node->keys.size();
            node->keys.push_back(0);

            while (pos > 0 && node->keys[pos - 1] > key) {
                node->keys[pos] = node->keys[pos - 1];
                pos--;
            }
            node->keys[pos] = key;
        } else {
            int i = 0;
            while (i < (int)node->keys.size() && key > node->keys[i]) {
                i++;
            }

            if ((int)node->kids[i]->keys.size() == 2 * t - 1) {
                splitChild(node, i, node->kids[i]);
                if (key > node->keys[i]) {
                    i++;
                }
            }

            insertNonFull(node->kids[i], key);
        }
    }

    bool findKey(Node* node, int key) {
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

        return findKey(node->kids[i], key);
    }

    void inorder(Node* node) {
        int i;
        for (i = 0; i < (int)node->keys.size(); i++) {
            if (!node->isLeaf) {
                inorder(node->kids[i]);
            }
            cout << node->keys[i] << " ";
        }

        if (!node->isLeaf) {
            inorder(node->kids[i]);
        }
    }

public:
    BTree(int minDegree) {
        t = minDegree;
        root = new Node(true);
    }

    void insert(int key) {
        if ((int)root->keys.size() == 2 * t - 1) {
            Node* newRoot = new Node(false);
            newRoot->kids.push_back(root);
            splitChild(newRoot, 0, root);
            root = newRoot;
        }
        insertNonFull(root, key);
    }

    bool search(int key) {
        return findKey(root, key);
    }

    void display() {
        inorder(root);
        cout << endl;
    }
};

int main() {
    int t;
    cout << "Enter minimum degree (t >= 2): ";
    cin >> t;

    BTree tree(t);

    int option, key;

    while (true) {
        cout << "\n--- B-Tree Operations ---\n";
        cout << "1) Insert\n2) Search\n3) Display (inorder)\n4) Quit\n";
        cout << "Choose: ";
        cin >> option;

        if (option == 1) {
            cout << "Key to insert: ";
            cin >> key;
            tree.insert(key);
        } else if (option == 2) {
            cout << "Key to search: ";
            cin >> key;
            if (tree.search(key)) {
                cout << "Key is present in the tree.\n";
            } else {
                cout << "Key is not in the tree.\n";
            }
        } else if (option == 3) {
            cout << "B-Tree (inorder): ";
            tree.display();
        } else if (option == 4) {
            cout << "Goodbye.\n";
            break;
        } else {
            cout << "Invalid option, try again.\n";
        }
    }

    return 0;
}
