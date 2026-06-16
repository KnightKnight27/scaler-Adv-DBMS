#include <iostream>
#include <string>

using namespace std;

class BTreeNode {
    int *keys;
    string *values;
    int t;
    BTreeNode **C;
    int n;
    bool leaf;

public:
    BTreeNode(int _t, bool _leaf);
    void traverse(int level = 0);
    BTreeNode* search(int k, string& out_value);
    void insertNonFull(int k, string v);
    void splitChild(int i, BTreeNode *y);

    friend class BTree;
};

class BTree {
    BTreeNode *root;
    int t;

public:
    BTree(int _t) {
        root = nullptr;
        t = _t;
    }

    void traverse() {
        if (root != nullptr)
            root->traverse();
    }

    BTreeNode* search(int k, string& out_value) {
        if (root == nullptr) return nullptr;
        return root->search(k, out_value);
    }

    void insert(int k, string v);
};

BTreeNode::BTreeNode(int _t, bool _leaf) {
    t = _t;
    leaf = _leaf;
    keys = new int[2 * t - 1];
    values = new string[2 * t - 1];
    C = new BTreeNode *[2 * t];
    n = 0;
}

void BTreeNode::traverse(int level) {
    int i;
    for (i = 0; i < n; i++) {
        if (!leaf) {
            C[i]->traverse(level + 1);
        }
        for (int j = 0; j < level; j++) cout << "    ";
        cout << "[" << keys[i] << " : " << values[i] << "]" << endl;
    }
    if (!leaf) {
        C[i]->traverse(level + 1);
    }
}

BTreeNode* BTreeNode::search(int k, string& out_value) {
    int i = 0;
    while (i < n && k > keys[i])
        i++;

    if (i < n && keys[i] == k) {
        out_value = values[i];
        return this;
    }

    if (leaf == true)
        return nullptr;

    return C[i]->search(k, out_value);
}

void BTree::insert(int k, string v) {
    if (root == nullptr) {
        root = new BTreeNode(t, true);
        root->keys[0] = k;
        root->values[0] = v;
        root->n = 1;
    } else {
        if (root->n == 2 * t - 1) {
            BTreeNode *s = new BTreeNode(t, false);
            s->C[0] = root;
            s->splitChild(0, root);
            int i = 0;
            if (s->keys[0] < k)
                i++;
            s->C[i]->insertNonFull(k, v);
            root = s;
        } else {
            root->insertNonFull(k, v);
        }
    }
}

void BTreeNode::insertNonFull(int k, string v) {
    int i = n - 1;
    if (leaf == true) {
        while (i >= 0 && keys[i] > k) {
            keys[i + 1] = keys[i];
            values[i + 1] = values[i];
            i--;
        }
        keys[i + 1] = k;
        values[i + 1] = v;
        n = n + 1;
    } else {
        while (i >= 0 && keys[i] > k)
            i--;
        if (C[i + 1]->n == 2 * t - 1) {
            splitChild(i + 1, C[i + 1]);
            if (keys[i + 1] < k)
                i++;
        }
        C[i + 1]->insertNonFull(k, v);
    }
}

void BTreeNode::splitChild(int i, BTreeNode *y) {
    BTreeNode *z = new BTreeNode(y->t, y->leaf);
    z->n = t - 1;

    for (int j = 0; j < t - 1; j++) {
        z->keys[j] = y->keys[j + t];
        z->values[j] = y->values[j + t];
    }

    if (y->leaf == false) {
        for (int j = 0; j < t; j++)
            z->C[j] = y->C[j + t];
    }

    y->n = t - 1;

    for (int j = n; j >= i + 1; j--)
        C[j + 1] = C[j];

    C[i + 1] = z;

    for (int j = n - 1; j >= i; j--) {
        keys[j + 1] = keys[j];
        values[j + 1] = values[j];
    }

    keys[i] = y->keys[t - 1];
    values[i] = y->values[t - 1];
    n = n + 1;
}

int main() {
    int degree = 3;
    BTree t(degree);
    
    cout << "B-Tree Initialization:" << endl;
    cout << "Initialized B-Tree with Minimum Degree (t) = " << degree << endl;
    cout << "Max keys per node: " << 2 * degree - 1 << endl;
    cout << "Min keys per node (excluding root): " << degree - 1 << endl;
    cout << endl;

    cout << "Record Insertion and Node Splitting:" << endl;
    int keysToInsert[] = {10, 20, 5, 6, 12, 30, 7, 17, 1, 3, 14, 25};
    for (int k : keysToInsert) {
        t.insert(k, "Value_" + to_string(k));
        cout << "Inserted Key: " << k << endl;
    }
    cout << endl;

    cout << "Tree Structure Analysis:" << endl;
    cout << "B-Tree structure (Indentation represents depth):" << endl;
    t.traverse();
    cout << endl;

    cout << "Search Operations and Indexing Behavior:" << endl;
    int searchKeys[] = {6, 17, 100};
    for (int k : searchKeys) {
        string val;
        cout << "Searching for key " << k << " -> ";
        BTreeNode* res = t.search(k, val);
        if (res != nullptr) {
            cout << "Found Record: [" << k << " : " << val << "]" << endl;
        } else {
            cout << "Key not found." << endl;
        }
    }
    cout << endl;

    return 0;
}
