// main.cpp — Lab 6
// Tanishq Singh | 24BCS10303
//
// B-Tree (minimum degree t) with full insert, search, delete, and two display modes.
// Split happens proactively on the way down during insert so we never backtrack.
// Delete ensures every child we descend into has at least t keys before going in,
// which means we can always remove a key without violating the invariants afterward.

#include <iostream>
#include <vector>
#include <queue>
using namespace std;

struct Node {
    bool isLeaf;
    vector<int> keys;
    vector<Node*> ch;   // children

    Node(bool leaf) : isLeaf(leaf) {}
};

class BTree {
    Node* root;
    int t;  // minimum degree — each non-root node has t-1..2t-1 keys

    // ---- split helpers ----

    // child ch[i] of par is full (2t-1 keys). Split it:
    // median goes up into par, right half becomes a new sibling.
    void splitChild(Node* par, int i) {
        Node* y = par->ch[i];
        Node* z = new Node(y->isLeaf);
        int mid = t - 1;

        for (int j = mid + 1; j < (int)y->keys.size(); j++)
            z->keys.push_back(y->keys[j]);
        if (!y->isLeaf)
            for (int j = mid + 1; j < (int)y->ch.size(); j++)
                z->ch.push_back(y->ch[j]);

        int median = y->keys[mid];
        y->keys.resize(mid);
        if (!y->isLeaf) y->ch.resize(mid + 1);

        par->ch.insert(par->ch.begin() + i + 1, z);
        par->keys.insert(par->keys.begin() + i, median);
    }

    // insert k into a subtree rooted at x, which is guaranteed non-full
    void insertNF(Node* x, int k) {
        if (x->isLeaf) {
            x->keys.push_back(k);
            int i = (int)x->keys.size() - 1;
            while (i > 0 && x->keys[i - 1] > k) {
                x->keys[i] = x->keys[i - 1];
                i--;
            }
            x->keys[i] = k;
        } else {
            int i = (int)x->keys.size() - 1;
            while (i >= 0 && x->keys[i] > k) i--;
            i++;
            if ((int)x->ch[i]->keys.size() == 2 * t - 1) {
                splitChild(x, i);
                if (k > x->keys[i]) i++;
            }
            insertNF(x->ch[i], k);
        }
    }

    // ---- search ----

    bool find(Node* x, int k) {
        int i = 0;
        while (i < (int)x->keys.size() && k > x->keys[i]) i++;
        if (i < (int)x->keys.size() && x->keys[i] == k) return true;
        if (x->isLeaf) return false;
        return find(x->ch[i], k);
    }

    // ---- traversal ----

    void inorder(Node* x) {
        for (int i = 0; i < (int)x->keys.size(); i++) {
            if (!x->isLeaf) inorder(x->ch[i]);
            cout << x->keys[i] << " ";
        }
        if (!x->isLeaf) inorder(x->ch[x->keys.size()]);
    }

    // ---- delete helpers ----

    // largest key in the subtree rooted at x->ch[idx]
    int pred(Node* x, int idx) {
        Node* cur = x->ch[idx];
        while (!cur->isLeaf) cur = cur->ch.back();
        return cur->keys.back();
    }

    // smallest key in the subtree rooted at x->ch[idx+1]
    int succ(Node* x, int idx) {
        Node* cur = x->ch[idx + 1];
        while (!cur->isLeaf) cur = cur->ch.front();
        return cur->keys.front();
    }

    // merge ch[idx] and ch[idx+1], pulling x->keys[idx] down as separator.
    // ch[idx+1] is deleted; all its content goes into ch[idx].
    void merge(Node* x, int idx) {
        Node* L = x->ch[idx];
        Node* R = x->ch[idx + 1];

        L->keys.push_back(x->keys[idx]);
        for (int k : R->keys) L->keys.push_back(k);
        if (!L->isLeaf) for (Node* c : R->ch) L->ch.push_back(c);

        x->keys.erase(x->keys.begin() + idx);
        x->ch.erase(x->ch.begin() + idx + 1);
        delete R;
    }

    // ch[idx] borrows one key from its left sibling ch[idx-1]
    // through the parent separator.
    void rotateRight(Node* x, int idx) {
        Node* child = x->ch[idx];
        Node* sib   = x->ch[idx - 1];

        child->keys.insert(child->keys.begin(), x->keys[idx - 1]);
        if (!child->isLeaf) {
            child->ch.insert(child->ch.begin(), sib->ch.back());
            sib->ch.pop_back();
        }
        x->keys[idx - 1] = sib->keys.back();
        sib->keys.pop_back();
    }

    // ch[idx] borrows one key from its right sibling ch[idx+1]
    // through the parent separator.
    void rotateLeft(Node* x, int idx) {
        Node* child = x->ch[idx];
        Node* sib   = x->ch[idx + 1];

        child->keys.push_back(x->keys[idx]);
        if (!child->isLeaf) {
            child->ch.push_back(sib->ch.front());
            sib->ch.erase(sib->ch.begin());
        }
        x->keys[idx] = sib->keys.front();
        sib->keys.erase(sib->keys.begin());
    }

    // make sure ch[idx] has at least t keys before we descend into it.
    // tries to borrow from a sibling first; merges if neither can spare one.
    void fill(Node* x, int idx) {
        if (idx > 0 && (int)x->ch[idx - 1]->keys.size() >= t) {
            rotateRight(x, idx);
        } else if (idx < (int)x->keys.size() && (int)x->ch[idx + 1]->keys.size() >= t) {
            rotateLeft(x, idx);
        } else {
            if (idx < (int)x->keys.size())
                merge(x, idx);
            else
                merge(x, idx - 1);
        }
    }

    // recursive delete of key k from the subtree rooted at x
    void del(Node* x, int k) {
        int idx = 0;
        while (idx < (int)x->keys.size() && k > x->keys[idx]) idx++;

        if (idx < (int)x->keys.size() && x->keys[idx] == k) {
            // key found in this node
            if (x->isLeaf) {
                // Case 1: leaf — just remove it
                x->keys.erase(x->keys.begin() + idx);
            } else if ((int)x->ch[idx]->keys.size() >= t) {
                // Case 2a: left child has room — replace with predecessor
                int p = pred(x, idx);
                x->keys[idx] = p;
                del(x->ch[idx], p);
            } else if ((int)x->ch[idx + 1]->keys.size() >= t) {
                // Case 2b: right child has room — replace with successor
                int s = succ(x, idx);
                x->keys[idx] = s;
                del(x->ch[idx + 1], s);
            } else {
                // Case 2c: both children are at minimum — merge and recurse
                merge(x, idx);
                del(x->ch[idx], k);
            }
        } else {
            // key not in this node — descend into the right child
            if (x->isLeaf) return;  // key isn't in the tree at all

            bool lastChild = (idx == (int)x->keys.size());
            if ((int)x->ch[idx]->keys.size() < t) {
                fill(x, idx);
                // if the last child merged with its left sibling, the index shifted
                if (lastChild && idx > (int)x->keys.size()) idx--;
            }
            del(x->ch[idx], k);
        }
    }

    void destroy(Node* x) {
        if (!x) return;
        if (!x->isLeaf) for (Node* c : x->ch) destroy(c);
        delete x;
    }

public:
    BTree(int deg) : root(new Node(true)), t(deg) {}
    ~BTree() { destroy(root); }

    void insert(int k) {
        if ((int)root->keys.size() == 2 * t - 1) {
            // root is full — tree grows up by one level
            Node* s = new Node(false);
            s->ch.push_back(root);
            splitChild(s, 0);
            root = s;
        }
        insertNF(root, k);
    }

    bool search(int k) { return find(root, k); }

    void remove(int k) {
        if (root->keys.empty()) return;
        del(root, k);
        // if a merge left the root empty, its only child becomes the new root
        if (root->keys.empty() && !root->isLeaf) {
            Node* old = root;
            root = root->ch[0];
            old->ch.clear();
            delete old;
        }
    }

    void display() {
        inorder(root);
        cout << "\n";
    }

    void levelOrder() {
        if (root->keys.empty()) { cout << "(empty)\n"; return; }
        queue<pair<Node*, int>> q;
        q.push({root, 0});
        int curLvl = -1;
        while (!q.empty()) {
            auto [node, lvl] = q.front(); q.pop();
            if (lvl != curLvl) {
                if (curLvl != -1) cout << "\n";
                cout << "L" << lvl << ":  ";
                curLvl = lvl;
            }
            cout << "[";
            for (int i = 0; i < (int)node->keys.size(); i++) {
                cout << node->keys[i];
                if (i + 1 < (int)node->keys.size()) cout << ", ";
            }
            cout << "]  ";
            if (!node->isLeaf)
                for (Node* c : node->ch) q.push({c, lvl + 1});
        }
        cout << "\n";
    }
};

int main() {
    cout << "=============================================\n";
    cout << "  Lab 6 — B-Tree (C++17)\n";
    cout << "  Tanishq Singh | 24BCS10303\n";
    cout << "=============================================\n\n";

    int t;
    cout << "Enter minimum degree t (>= 2): ";
    if (!(cin >> t) || t < 2) {
        cout << "t must be >= 2.\n";
        return 1;
    }

    BTree tree(t);
    int opt, k;

    while (true) {
        cout << "\n--- B-Tree Menu ---\n";
        cout << "1. Insert\n";
        cout << "2. Delete\n";
        cout << "3. Search\n";
        cout << "4. Inorder display\n";
        cout << "5. Level-order display\n";
        cout << "6. Exit\n";
        cout << "> ";
        if (!(cin >> opt)) break;

        if (opt == 1) {
            cout << "key: "; cin >> k;
            tree.insert(k);
            cout << "inserted " << k << "\n";
        } else if (opt == 2) {
            cout << "key: "; cin >> k;
            tree.remove(k);
            cout << "done\n";
        } else if (opt == 3) {
            cout << "key: "; cin >> k;
            cout << (tree.search(k) ? "found\n" : "not found\n");
        } else if (opt == 4) {
            cout << "inorder: ";
            tree.display();
        } else if (opt == 5) {
            tree.levelOrder();
        } else if (opt == 6) {
            cout << "bye.\n";
            break;
        } else {
            cout << "invalid option\n";
        }
    }
    return 0;
}
