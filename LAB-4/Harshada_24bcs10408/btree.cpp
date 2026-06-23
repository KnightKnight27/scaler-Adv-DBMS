#include <iostream>
#include <vector>

using namespace std;

const int T = 2;

struct BNode {
    vector<int> keys;
    vector<BNode*> children;
    bool leaf;

    BNode(bool leaf = true) {
        this->leaf = leaf;
    }
};

class BTree {
private:
    BNode* root;

    void traverse(BNode* node) {
        if (!node) return;

        int i;
        for (i = 0; i < node->keys.size(); i++) {
            if (!node->leaf)
                traverse(node->children[i]);

            cout << node->keys[i] << " ";
        }

        if (!node->leaf)
            traverse(node->children[i]);
    }

    bool search(BNode* node, int key) {
        int i = 0;

        while (i < node->keys.size() && key > node->keys[i])
            i++;

        if (i < node->keys.size() && node->keys[i] == key)
            return true;

        if (node->leaf)
            return false;

        return search(node->children[i], key);
    }

    void splitChild(BNode* parent, int i) {
        BNode* y = parent->children[i];
        BNode* z = new BNode(y->leaf);

        int median = y->keys[T - 1];

        for (int j = T; j < y->keys.size(); j++)
            z->keys.push_back(y->keys[j]);

        if (!y->leaf) {
            for (int j = T; j < y->children.size(); j++)
                z->children.push_back(y->children[j]);
        }

        y->keys.resize(T - 1);

        if (!y->leaf)
            y->children.resize(T);

        parent->children.insert(
            parent->children.begin() + i + 1,
            z
        );

        parent->keys.insert(
            parent->keys.begin() + i,
            median
        );
    }

    void insertNonFull(BNode* node, int key) {
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
            while (i >= 0 && key < node->keys[i])
                i--;

            i++;

            if (node->children[i]->keys.size() == 2 * T - 1) {
                splitChild(node, i);

                if (key > node->keys[i])
                    i++;
            }

            insertNonFull(node->children[i], key);
        }
    }

    int getPredecessor(BNode* node, int idx) {
        BNode* cur = node->children[idx];

        while (!cur->leaf)
            cur = cur->children.back();

        return cur->keys.back();
    }

    int getSuccessor(BNode* node, int idx) {
        BNode* cur = node->children[idx + 1];

        while (!cur->leaf)
            cur = cur->children.front();

        return cur->keys.front();
    }

    void merge(BNode* node, int idx) {
        BNode* child = node->children[idx];
        BNode* sibling = node->children[idx + 1];

        child->keys.push_back(node->keys[idx]);

        for (int k : sibling->keys)
            child->keys.push_back(k);

        if (!child->leaf) {
            for (auto c : sibling->children)
                child->children.push_back(c);
        }

        node->keys.erase(node->keys.begin() + idx);

        node->children.erase(
            node->children.begin() + idx + 1
        );

        delete sibling;
    }

    void borrowFromPrev(BNode* node, int idx) {
        BNode* child = node->children[idx];
        BNode* sibling = node->children[idx - 1];

        child->keys.insert(
            child->keys.begin(),
            node->keys[idx - 1]
        );

        if (!child->leaf) {
            child->children.insert(
                child->children.begin(),
                sibling->children.back()
            );

            sibling->children.pop_back();
        }

        node->keys[idx - 1] = sibling->keys.back();
        sibling->keys.pop_back();
    }

    void borrowFromNext(BNode* node, int idx) {
        BNode* child = node->children[idx];
        BNode* sibling = node->children[idx + 1];

        child->keys.push_back(node->keys[idx]);

        if (!child->leaf) {
            child->children.push_back(
                sibling->children.front()
            );

            sibling->children.erase(
                sibling->children.begin()
            );
        }

        node->keys[idx] = sibling->keys.front();

        sibling->keys.erase(
            sibling->keys.begin()
        );
    }

    void fill(BNode* node, int idx) {
        if (idx != 0 &&
            node->children[idx - 1]->keys.size() >= T)
            borrowFromPrev(node, idx);

        else if (
            idx != node->keys.size() &&
            node->children[idx + 1]->keys.size() >= T)
            borrowFromNext(node, idx);

        else {
            if (idx != node->keys.size())
                merge(node, idx);
            else
                merge(node, idx - 1);
        }
    }

    void removeNode(BNode* node, int key) {
        int idx = 0;

        while (idx < node->keys.size() &&
               node->keys[idx] < key)
            idx++;

        if (idx < node->keys.size() &&
            node->keys[idx] == key) {

            if (node->leaf) {
                node->keys.erase(
                    node->keys.begin() + idx
                );
            }
            else {
                if (node->children[idx]->keys.size() >= T) {
                    int pred = getPredecessor(node, idx);
                    node->keys[idx] = pred;
                    removeNode(node->children[idx], pred);
                }
                else if (
                    node->children[idx + 1]->keys.size() >= T) {

                    int succ = getSuccessor(node, idx);
                    node->keys[idx] = succ;
                    removeNode(node->children[idx + 1], succ);
                }
                else {
                    merge(node, idx);
                    removeNode(node->children[idx], key);
                }
            }
        }
        else {
            if (node->leaf) {
                cout << "Key not found\n";
                return;
            }

            bool flag =
                (idx == node->keys.size());

            if (node->children[idx]->keys.size() < T)
                fill(node, idx);

            if (flag &&
                idx > node->keys.size())
                removeNode(node->children[idx - 1], key);
            else
                removeNode(node->children[idx], key);
        }
    }

public:
    BTree() {
        root = nullptr;
    }

    void insert(int key) {
        if (!root) {
            root = new BNode(true);
            root->keys.push_back(key);
            return;
        }

        if (root->keys.size() == 2 * T - 1) {
            BNode* s = new BNode(false);

            s->children.push_back(root);

            splitChild(s, 0);

            int i = 0;

            if (s->keys[0] < key)
                i++;

            insertNonFull(s->children[i], key);

            root = s;
        }
        else {
            insertNonFull(root, key);
        }
    }

    bool search(int key) {
        if (!root) return false;
        return search(root, key);
    }

    void remove(int key) {
        if (!root) return;

        removeNode(root, key);

        if (root->keys.empty()) {
            BNode* tmp = root;

            if (root->leaf)
                root = nullptr;
            else
                root = root->children[0];

            delete tmp;
        }
    }

    void print() {
        traverse(root);
        cout << endl;
    }
};

int main() {
    BTree bt;

    int arr[] = {
        10,20,5,6,12,
        30,7,17,3,1,25
    };

    for (int x : arr)
        bt.insert(x);

    cout << "Inorder after inserts:\n";
    bt.print();

    cout << "Search 17: "
         << (bt.search(17) ? "found" : "not found")
         << endl;

    cout << "Search 99: "
         << (bt.search(99) ? "found" : "not found")
         << endl;

    bt.remove(6);
    bt.remove(20);

    cout << "\nAfter deleting 6 and 20:\n";
    bt.print();

    return 0;
}