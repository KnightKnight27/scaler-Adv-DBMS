#include <iostream>
#include <vector>
#include <string>
#include <optional>
using namespace std;

// B-Tree with minimum degree t.
//   Each node holds [t-1 .. 2t-1] keys (root may hold 1).
//   Each internal node holds [t .. 2t] children.
template<typename Key, typename Row>
class BTree {
private:
    struct Entry {
        Key key;
        Row row;
        Entry(Key k, Row r) : key(k), row(r) {}
    };

    struct Node {
        vector<Entry>  keys;
        vector<Node*>  children;
        bool           isLeaf;
        explicit Node(bool leaf) : isLeaf(leaf) {}
    };

    Node*  root;
    int    t;      // minimum degree

    // ── Split child[i] of `parent` (child must be full: 2t-1 keys) ──
    void splitChild(Node* parent, int i, Node* child) {
        int mid = t - 1;
        cout << "[SPLIT]  Promoting key=" << child->keys[mid].key
             << " from full node\n";

        Node* right = new Node(child->isLeaf);

        // Move keys after median to right sibling
        for (int j = mid + 1; j < (int)child->keys.size(); j++)
            right->keys.push_back(child->keys[j]);

        if (!child->isLeaf)
            for (int j = t; j < (int)child->children.size(); j++)
                right->children.push_back(child->children[j]);

        // Promote median key to parent
        parent->keys.insert(parent->keys.begin() + i, child->keys[mid]);
        parent->children.insert(parent->children.begin() + i + 1, right);

        child->keys.resize(mid);
        if (!child->isLeaf) child->children.resize(t);
    }

    // ── Insert into a node that is guaranteed non-full ───────────────
    void insertNonFull(Node* node, Key key, Row row) {
        int i = (int)node->keys.size() - 1;

        if (node->isLeaf) {
            node->keys.emplace_back(key, row);
            while (i >= 0 && node->keys[i].key > key) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }
            node->keys[i + 1] = Entry(key, row);
            cout << "[LEAF INSERT] key=" << key << "\n";
        } else {
            while (i >= 0 && node->keys[i].key > key) i--;
            i++;
            if ((int)node->children[i]->keys.size() == 2 * t - 1) {
                splitChild(node, i, node->children[i]);
                if (key > node->keys[i].key) i++;
            }
            insertNonFull(node->children[i], key, row);
        }
    }

    // ── Recursive search ─────────────────────────────────────────────
    optional<Row> searchHelper(Node* node, Key key) const {
        int i = 0;
        while (i < (int)node->keys.size() && key > node->keys[i].key) i++;
        if (i < (int)node->keys.size() && key == node->keys[i].key)
            return node->keys[i].row;
        if (node->isLeaf) return nullopt;
        return searchHelper(node->children[i], key);
    }

    // ── Pretty-print ─────────────────────────────────────────────────
    void printHelper(Node* node, int depth) const {
        string indent(depth * 4, ' ');
        cout << indent << "[";
        for (int i = 0; i < (int)node->keys.size(); i++) {
            if (i) cout << " | ";
            cout << node->keys[i].key;
        }
        cout << "]\n";
        for (Node* child : node->children)
            printHelper(child, depth + 1);
    }

public:
    explicit BTree(int degree) : t(degree) {
        root = new Node(true);
        cout << "[INIT] B-Tree  t=" << t
             << "  max_keys=" << (2*t-1)
             << "  min_keys=" << (t-1) << "\n\n";
    }

    // Task 2 & 3: Insert key-value pair.
    void insert(Key key, Row row) {
        Node* r = root;
        if ((int)r->keys.size() == 2 * t - 1) {
            // Root is full – grow the tree upward
            Node* newRoot = new Node(false);
            newRoot->children.push_back(root);
            splitChild(newRoot, 0, root);
            root = newRoot;
            cout << "[GROW]   Tree height increased\n";
        }
        insertNonFull(root, key, row);
    }

    // Task 4 & 6: Search by key.
    bool search(Key key) const {
        auto result = searchHelper(root, key);
        if (result) {
            cout << "[FOUND]     key=" << key << "  row=\"" << *result << "\"\n";
            return true;
        }
        cout << "[NOT FOUND] key=" << key << "\n";
        return false;
    }

    // Task 5: Print tree structure level by level.
    void print() const {
        cout << "B-Tree structure (each level indented):\n";
        printHelper(root, 0);
    }
};

int main() {
    cout << "=== Lab 6: B-Tree Index Implementation ===\n\n";

    // Task 1: Initialize with minimum degree 3 (max 5 keys/node)
    cout << "--- Task 1: Initialization ---\n";
    BTree<int, string> db(3);

    // Task 2 & 3: Insertions – observe splits as nodes fill up
    cout << "--- Task 2 & 3: Record Insertion ---\n";
    db.insert(10, "Alice");
    db.insert(20, "Bob");
    db.insert(5,  "Carol");
    db.insert(15, "David");
    db.insert(25, "Eva");
    db.insert(30, "Frank");
    db.insert(1,  "Grace");
    db.insert(12, "Henry");
    db.insert(18, "Iris");
    db.insert(22, "Jack");
    cout << "\n";

    // Task 5: Tree structure
    cout << "--- Task 5: Tree Structure ---\n";
    db.print();
    cout << "\n";

    // Task 4 & 6: Search / indexing behavior
    cout << "--- Task 4 & 6: Search Operations ---\n";
    db.search(15);
    db.search(1);
    db.search(99);

    return 0;
}
