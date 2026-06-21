#include <iostream>
#include <vector>
#include <queue>
using namespace std;

struct Node {
    bool isLeaf;
    vector<int> keys;
    vector<Node*> kids;

    Node(bool leaf) : isLeaf(leaf) {}
};

class BTree {
    Node* root;
    int t;  // minimum degree

    // ---------- insert helpers ----------

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

    // ---------- search ----------

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

    // ---------- inorder ----------

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

    // ---------- delete helpers ----------

    int findKeyIdx(Node* node, int key) {
        int idx = 0;
        while (idx < (int)node->keys.size() && node->keys[idx] < key) {
            idx++;
        }
        return idx;
    }

    void removeFromLeaf(Node* node, int idx) {
        node->keys.erase(node->keys.begin() + idx);
    }

    int getPredecessor(Node* node, int idx) {
        Node* cur = node->kids[idx];
        while (!cur->isLeaf) {
            cur = cur->kids.back();
        }
        return cur->keys.back();
    }

    int getSuccessor(Node* node, int idx) {
        Node* cur = node->kids[idx + 1];
        while (!cur->isLeaf) {
            cur = cur->kids.front();
        }
        return cur->keys.front();
    }

    void merge(Node* node, int idx) {
        Node* child = node->kids[idx];
        Node* sibling = node->kids[idx + 1];

        // Pull parent's separator down into child
        child->keys.push_back(node->keys[idx]);

        // Append sibling's keys and children
        for (int k : sibling->keys) {
            child->keys.push_back(k);
        }
        if (!child->isLeaf) {
            for (Node* c : sibling->kids) {
                child->kids.push_back(c);
            }
        }

        node->keys.erase(node->keys.begin() + idx);
        node->kids.erase(node->kids.begin() + idx + 1);

        delete sibling;
    }

    void borrowFromPrev(Node* node, int idx) {
        Node* child = node->kids[idx];
        Node* sibling = node->kids[idx - 1];

        // Rotate: parent's separator goes to front of child;
        //         sibling's last key goes up to parent.
        child->keys.insert(child->keys.begin(), node->keys[idx - 1]);

        if (!child->isLeaf) {
            child->kids.insert(child->kids.begin(), sibling->kids.back());
            sibling->kids.pop_back();
        }

        node->keys[idx - 1] = sibling->keys.back();
        sibling->keys.pop_back();
    }

    void borrowFromNext(Node* node, int idx) {
        Node* child = node->kids[idx];
        Node* sibling = node->kids[idx + 1];

        child->keys.push_back(node->keys[idx]);

        if (!child->isLeaf) {
            child->kids.push_back(sibling->kids.front());
            sibling->kids.erase(sibling->kids.begin());
        }

        node->keys[idx] = sibling->keys.front();
        sibling->keys.erase(sibling->keys.begin());
    }

    void fillChild(Node* node, int idx) {
        if (idx != 0 && (int)node->kids[idx - 1]->keys.size() >= t) {
            borrowFromPrev(node, idx);
        } else if (idx != (int)node->keys.size() && (int)node->kids[idx + 1]->keys.size() >= t) {
            borrowFromNext(node, idx);
        } else {
            if (idx != (int)node->keys.size()) {
                merge(node, idx);
            } else {
                merge(node, idx - 1);
            }
        }
    }

    void removeFromInternal(Node* node, int idx) {
        int key = node->keys[idx];

        if ((int)node->kids[idx]->keys.size() >= t) {
            int pred = getPredecessor(node, idx);
            node->keys[idx] = pred;
            removeFromNode(node->kids[idx], pred);
        } else if ((int)node->kids[idx + 1]->keys.size() >= t) {
            int succ = getSuccessor(node, idx);
            node->keys[idx] = succ;
            removeFromNode(node->kids[idx + 1], succ);
        } else {
            merge(node, idx);
            removeFromNode(node->kids[idx], key);
        }
    }

    void removeFromNode(Node* node, int key) {
        int idx = findKeyIdx(node, key);

        if (idx < (int)node->keys.size() && node->keys[idx] == key) {
            if (node->isLeaf) {
                removeFromLeaf(node, idx);
            } else {
                removeFromInternal(node, idx);
            }
        } else {
            if (node->isLeaf) {
                return;  // not in tree
            }

            bool isLastChild = (idx == (int)node->keys.size());

            if ((int)node->kids[idx]->keys.size() < t) {
                fillChild(node, idx);
            }

            // If the last child merged with its previous sibling, that position no longer exists
            if (isLastChild && idx > (int)node->keys.size()) {
                removeFromNode(node->kids[idx - 1], key);
            } else {
                removeFromNode(node->kids[idx], key);
            }
        }
    }

    // ---------- destruction ----------

    void destroy(Node* node) {
        if (!node) return;
        if (!node->isLeaf) {
            for (Node* c : node->kids) {
                destroy(c);
            }
        }
        delete node;
    }

public:
    BTree(int minDegree) : root(new Node(true)), t(minDegree) {}

    ~BTree() {
        destroy(root);
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

    void remove(int key) {
        if (root->keys.empty()) {
            return;
        }

        removeFromNode(root, key);

        // If the root ended up empty, shrink the tree by one level
        if (root->keys.empty()) {
            Node* old = root;
            if (root->isLeaf) {
                root = new Node(true);  // empty tree
            } else {
                root = root->kids[0];
            }
            delete old;
        }
    }

    bool search(int key) {
        return findKey(root, key);
    }

    void display() {
        inorder(root);
        cout << endl;
    }

    void displayLevels() {
        if (root->keys.empty()) {
            cout << "(empty tree)\n";
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
                if (currentLevel != -1) cout << '\n';
                cout << "L" << level << ":  ";
                currentLevel = level;
            }

            cout << "[";
            for (size_t i = 0; i < node->keys.size(); i++) {
                cout << node->keys[i];
                if (i + 1 < node->keys.size()) cout << ", ";
            }
            cout << "]  ";

            if (!node->isLeaf) {
                for (Node* c : node->kids) {
                    q.push({c, level + 1});
                }
            }
        }
        cout << '\n';
    }
};

int main() {
    int t;
    cout << "Enter minimum degree (t >= 2): ";
    cin >> t;

    if (t < 2) {
        cout << "Invalid: t must be at least 2.\n";
        return 1;
    }

    BTree tree(t);

    int option, key;

    while (true) {
        cout << "\n--- B-Tree Operations ---\n";
        cout << "1) Insert\n";
        cout << "2) Delete\n";
        cout << "3) Search\n";
        cout << "4) Display (inorder)\n";
        cout << "5) Display (level-order)\n";
        cout << "6) Quit\n";
        cout << "Choose: ";
        cin >> option;

        if (option == 1) {
            cout << "Key to insert: ";
            cin >> key;
            tree.insert(key);
        } else if (option == 2) {
            cout << "Key to delete: ";
            cin >> key;
            tree.remove(key);
        } else if (option == 3) {
            cout << "Key to search: ";
            cin >> key;
            if (tree.search(key)) {
                cout << "Key is present in the tree.\n";
            } else {
                cout << "Key is not in the tree.\n";
            }
        } else if (option == 4) {
            cout << "B-Tree (inorder): ";
            tree.display();
        } else if (option == 5) {
            cout << "B-Tree (level-order):\n";
            tree.displayLevels();
        } else if (option == 6) {
            cout << "Goodbye.\n";
            break;
        } else {
            cout << "Invalid option, try again.\n";
        }
    }

    return 0;
}
