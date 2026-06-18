// Lab 6 - B-Tree Index
// Siddhanth Kapoor (10154)
//
// a B-tree of minimum degree t: every node (except root) holds between t-1 and
// 2t-1 keys, all leaves sit at the same depth, and a full child is split before
// we descend into it. this is the shape real database indexes use to keep disk
// accesses logarithmic while packing many keys per node.

#include <iostream>
#include <string>
#include <vector>

template <int T>
class BTree {
public:
    BTree() : root(new Node(true)) {}
    ~BTree() { destroy(root); }

    void insert(int key, const std::string& value) {
        Node* r = root;
        if (r->n == 2 * T - 1) {          // root full -> grow the tree taller
            Node* s = new Node(false);
            s->child[0] = r;
            splitChild(s, 0);
            root = s;
            insertNonFull(s, key, value);
        } else {
            insertNonFull(r, key, value);
        }
    }

    // search returns the value for key, or "" if absent; prints the node path.
    std::string search(int key) const {
        std::cout << "search(" << key << "): ";
        std::string out = search(root, key);
        std::cout << (out.empty() ? "-> not found\n" : "-> found '" + out + "'\n");
        return out;
    }

    void print() const {
        std::cout << "B-tree (t=" << T << "), level by level:\n";
        print(root, 0);
    }

private:
    struct Node {
        bool leaf;
        int n = 0;                          // current number of keys
        int keys[2 * T - 1];
        std::string vals[2 * T - 1];
        Node* child[2 * T] = {};
        explicit Node(bool isLeaf) : leaf(isLeaf) {}
    };

    Node* root;

    // split child[i] of x (which must be full) around its median key.
    void splitChild(Node* x, int i) {
        Node* y = x->child[i];
        Node* z = new Node(y->leaf);
        z->n = T - 1;
        for (int j = 0; j < T - 1; ++j) {   // right half of y -> z
            z->keys[j] = y->keys[j + T];
            z->vals[j] = y->vals[j + T];
        }
        if (!y->leaf)
            for (int j = 0; j < T; ++j) z->child[j] = y->child[j + T];
        y->n = T - 1;

        for (int j = x->n; j >= i + 1; --j) x->child[j + 1] = x->child[j];
        x->child[i + 1] = z;
        for (int j = x->n - 1; j >= i; --j) {
            x->keys[j + 1] = x->keys[j];
            x->vals[j + 1] = x->vals[j];
        }
        x->keys[i] = y->keys[T - 1];        // median promoted into the parent
        x->vals[i] = y->vals[T - 1];
        x->n += 1;
    }

    void insertNonFull(Node* x, int key, const std::string& value) {
        int i = x->n - 1;
        if (x->leaf) {
            while (i >= 0 && key < x->keys[i]) {
                x->keys[i + 1] = x->keys[i];
                x->vals[i + 1] = x->vals[i];
                --i;
            }
            x->keys[i + 1] = key;
            x->vals[i + 1] = value;
            x->n += 1;
        } else {
            while (i >= 0 && key < x->keys[i]) --i;
            ++i;
            if (x->child[i]->n == 2 * T - 1) {
                splitChild(x, i);
                if (key > x->keys[i]) ++i;
            }
            insertNonFull(x->child[i], key, value);
        }
    }

    std::string search(Node* x, int key) const {
        int i = 0;
        while (i < x->n && key > x->keys[i]) ++i;
        std::cout << "[node:";
        for (int j = 0; j < x->n; ++j) std::cout << " " << x->keys[j];
        std::cout << "] ";
        if (i < x->n && key == x->keys[i]) return x->vals[i];
        if (x->leaf) return "";
        return search(x->child[i], key);
    }

    void print(Node* x, int depth) const {
        std::cout << std::string(depth * 2, ' ') << "L" << depth << ": ";
        for (int j = 0; j < x->n; ++j) std::cout << x->keys[j] << " ";
        std::cout << "\n";
        if (!x->leaf)
            for (int j = 0; j <= x->n; ++j) print(x->child[j], depth + 1);
    }

    void destroy(Node* x) {
        if (!x) return;
        if (!x->leaf)
            for (int j = 0; j <= x->n; ++j) destroy(x->child[j]);
        delete x;
    }
};

int main() {
    BTree<3> idx; // minimum degree 3: each node holds 2..5 keys

    const std::vector<std::pair<int, std::string>> records = {
        {10, "Aarav"}, {20, "Bhavna"}, {5, "Chetan"}, {6, "Diya"}, {12, "Esha"},
        {30, "Farhan"}, {7, "Gita"}, {17, "Hari"}, {3, "Isha"}, {25, "Jatin"},
        {40, "Kiran"}, {45, "Lata"}, {1, "Mohan"}, {50, "Nisha"}, {55, "Om"},
    };

    std::cout << "== inserting " << records.size() << " key/value records ==\n";
    for (const auto& r : records) idx.insert(r.first, r.second);

    std::cout << "\n== tree structure after insertion ==\n";
    idx.print();

    std::cout << "\n== searches ==\n";
    idx.search(6);   // present
    idx.search(45);  // present (deeper)
    idx.search(99);  // absent

    return 0;
}
