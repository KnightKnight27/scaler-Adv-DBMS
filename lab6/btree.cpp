#include <iostream>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <memory>

using namespace std;

class Node {
public:
    bool isLeaf;
    vector<int> keys;
    vector<Node*> children;

    Node(bool leaf) {
        isLeaf = leaf;
    }
};

class BTree {
private:
    Node* root;
    int t;

    void clear(Node* node) {
        if (node == nullptr) return;
        if (!node->isLeaf) {
            for (Node* child : node->children) {
                clear(child);
            }
        }
        delete node;
    }

    void splitChild(Node* x, int i, Node* y) {
        Node* z = new Node(y->isLeaf);
        
        z->keys.assign(y->keys.begin() + t, y->keys.end());
        
        if (!y->isLeaf) {
            z->children.assign(y->children.begin() + t, y->children.end());
        }
        
        int middleKey = y->keys[t - 1];
        
        y->keys.resize(t - 1);
        if (!y->isLeaf) {
            y->children.resize(t);
        }
        
        x->children.insert(x->children.begin() + i + 1, z);
        x->keys.insert(x->keys.begin() + i, middleKey);
    }

    void insertNonFull(Node* node, int key) {
        int i = node->keys.size() - 1;

        if (node->isLeaf) {
            auto it = upper_bound(node->keys.begin(), node->keys.end(), key);
            node->keys.insert(it, key);
        } else {
            while (i >= 0 && node->keys[i] > key) {
                i--;
            }
            i++;

            if (node->children[i]->keys.size() == 2 * t - 1) {
                splitChild(node, i, node->children[i]);
                
                if (node->keys[i] < key) {
                    i++;
                }
            }
            insertNonFull(node->children[i], key);
        }
    }

    bool searchHelper(Node* cursor, int key) {
        if (cursor == nullptr) return false;

        int i = 0;
        while (i < cursor->keys.size() && key > cursor->keys[i]) {
            i++;
        }

        if (i < cursor->keys.size() && cursor->keys[i] == key) {
            return true;
        }

        if (cursor->isLeaf) {
            return false;
        }

        return searchHelper(cursor->children[i], key);
    }

    void displayHelper(Node* cursor, int depth) {
        if (cursor == nullptr) return;

        for (int i = 0; i < depth; i++) {
            cout << "    ";
        }

        cout << "├─ [ ";
        for (size_t i = 0; i < cursor->keys.size(); i++) {
            cout << cursor->keys[i];
            if (i < cursor->keys.size() - 1) cout << ", ";
        }
        cout << " ]" << endl;

        if (!cursor->isLeaf) {
            for (Node* child : cursor->children) {
                displayHelper(child, depth + 1);
            }
        }
    }

public:
    BTree(int degree = 2) {
        root = nullptr;
        t = degree;
    }

    ~BTree() {
        clear(root);
    }

    void insert(int key) {
        if (root == nullptr) {
            root = new Node(true);
            root->keys.push_back(key);
            return;
        }

        if (root->keys.size() == 2 * t - 1) {
            Node* s = new Node(false);
            s->children.push_back(root);
            splitChild(s, 0, root);
            root = s;
            insertNonFull(root, key);
        } else {
            insertNonFull(root, key);
        }
    }

    bool search(int key) {
        return searchHelper(root, key);
    }

    void display() {
        if (root == nullptr) {
            cout << "Empty Tree" << endl;
            return;
        }
        displayHelper(root, 0);
    }
};

using BPlusTree = BTree;

int main() {
    cout << "=========================================" << endl;
    cout << "       B-Tree Visualizer & Lab Demo      " << endl;
    cout << "=========================================" << endl;

    int degree = 2;
    cout << "Creating a B-Tree of Degree t = " << degree << " (Max keys per node = " << (2 * degree - 1) << ")" << endl;
    BTree tree(degree);

    vector<int> testKeys = {10, 20, 30, 40, 50, 60, 70, 80};
    cout << "\n--- Step 1: Automatic Demo (Inserting " << testKeys.size() << " keys) ---" << endl;
    
    for (int key : testKeys) {
        cout << "\nInserting key: " << key << "..." << endl;
        tree.insert(key);
        tree.display();
    }

    cout << "\n--- Verification ---" << endl;
    vector<int> searchKeys = {30, 45, 70, 99};
    for (int key : searchKeys) {
        cout << "Searching for " << key << ": " << (tree.search(key) ? "FOUND ✓" : "NOT FOUND ✗") << endl;
    }

    cout << "\n=========================================" << endl;
    cout << "        Interactive Mode Enabled         " << endl;
    cout << "=========================================" << endl;

    int choice;
    while (true) {
        cout << "\nMenu Options:\n";
        cout << "1. Insert a Key\n";
        cout << "2. Search for a Key\n";
        cout << "3. Print Tree Structure\n";
        cout << "5. Exit\n";
        cout << "Enter your choice: ";
        
        if (!(cin >> choice)) {
            cout << "Invalid input. Exiting." << endl;
            break;
        }

        if (choice == 1) {
            int key;
            cout << "Enter key to insert: ";
            if (cin >> key) {
                tree.insert(key);
                cout << "Key inserted successfully!" << endl;
                cout << "\nUpdated Tree Structure:\n";
                tree.display();
            }
        } 
        else if (choice == 2) {
            int key;
            cout << "Enter key to search: ";
            if (cin >> key) {
                if (tree.search(key)) {
                    cout << "Key " << key << " exists in the B-Tree! ✓" << endl;
                } else {
                    cout << "Key " << key << " does not exist in the B-Tree. ✗" << endl;
                }
            }
        } 
        else if (choice == 3) {
            cout << "\nB-Tree Structure:\n";
            tree.display();
        } 
        else if (choice == 5) {
            cout << "Exiting Interactive Mode. Goodbye!" << endl;
            break;
        } 
        else {
            cout << "Invalid choice! Please select 1, 2, 3, or 5." << endl;
        }
    }

    return 0;
}
