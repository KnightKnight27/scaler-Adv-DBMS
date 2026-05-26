// Lab 6 - B-Tree Index
//
// A B-Tree used as a database index that maps a Key to a Row payload, mirroring
// how engines like SQLite store table/index data in balanced multi-way nodes.
//
// Design intent (from the reference DB<Key, Row> sketch):
//   - Each node holds up to (2t - 1) entries and up to 2t children.
//   - A node is "full" when it holds (2t - 1) entries; it is split before a
//     child is descended into, so insertion needs only a single downward pass.
//   - Search walks the separators in a node, then descends into the matching
//     child range, exactly like a disk-backed index probe.
//
//   layout of a node:  [ c0 (k0,row0) c1 (k1,row1) c2 ... (k_{n-1}) cn ]
//   keys in c_i are < k_i, keys in c_{i+1} are > k_i  (BST ordering, n-way)

#include <iostream>
#include <optional>
#include <vector>

template <typename Key, typename Row>
class DB {
private:
    struct Entry {
        Key key;
        Row row;
    };

    struct Node {
        bool                 leaf;
        std::vector<Entry>   entries;   // up to 2t - 1
        std::vector<Node*>   children;  // up to 2t (empty when leaf)
        explicit Node(bool isLeaf) : leaf(isLeaf) {}
    };

    Node*  root;
    size_t t;          // minimum degree
    size_t maxEntries; // 2t - 1

    bool isFull(Node* n) const { return n->entries.size() == maxEntries; }

    // Split child y = x->children[i], which is full, into two nodes and pull
    // its median entry up into x at position i.
    void splitChild(Node* x, size_t i) {
        Node* y = x->children[i];
        Node* z = new Node(y->leaf);

        // z takes the upper t-1 entries; y keeps the lower t-1; median is y[t-1]
        for (size_t j = 0; j < t - 1; j++)
            z->entries.push_back(y->entries[t + j]);

        if (!y->leaf) {
            for (size_t j = 0; j < t; j++)
                z->children.push_back(y->children[t + j]);
            y->children.resize(t);
        }

        Entry median = y->entries[t - 1];
        y->entries.resize(t - 1);

        x->children.insert(x->children.begin() + i + 1, z);
        x->entries.insert(x->entries.begin() + i, median);
    }

    // Insert into a node guaranteed not to be full.
    void insertNonFull(Node* x, const Key& key, const Row& row) {
        int i = static_cast<int>(x->entries.size()) - 1;

        if (x->leaf) {
            x->entries.push_back({key, row});
            while (i >= 0 && key < x->entries[i].key) {
                x->entries[i + 1] = x->entries[i];
                i--;
            }
            x->entries[i + 1] = {key, row};
            return;
        }

        while (i >= 0 && key < x->entries[i].key) i--;
        size_t c = static_cast<size_t>(i + 1);

        if (isFull(x->children[c])) {
            splitChild(x, c);
            if (key > x->entries[c].key) c++;
        }
        insertNonFull(x->children[c], key, row);
    }

    const Entry* searchNode(Node* x, const Key& key) const {
        size_t i = 0;
        while (i < x->entries.size() && key > x->entries[i].key) i++;
        if (i < x->entries.size() && key == x->entries[i].key)
            return &x->entries[i];
        if (x->leaf) return nullptr;
        return searchNode(x->children[i], key);
    }

    void inorder(Node* x, std::vector<Entry>& out) const {
        if (!x) return;
        size_t i = 0;
        for (; i < x->entries.size(); i++) {
            if (!x->leaf) inorder(x->children[i], out);
            out.push_back(x->entries[i]);
        }
        if (!x->leaf) inorder(x->children[i], out);
    }

    void printTree(Node* x, int depth) const {
        if (!x) return;
        std::cout << std::string(depth * 2, ' ') << "[";
        for (size_t i = 0; i < x->entries.size(); i++)
            std::cout << (i ? " " : "") << x->entries[i].key;
        std::cout << "]\n";
        for (Node* c : x->children) printTree(c, depth + 1);
    }

    void destroy(Node* x) {
        if (!x) return;
        for (Node* c : x->children) destroy(c);
        delete x;
    }

public:
    explicit DB(size_t degree) : t(degree), maxEntries(2 * degree - 1) {
        root = new Node(true);
    }
    ~DB() { destroy(root); }

    void Insert(const Key& key, const Row& row) {
        if (isFull(root)) {
            Node* s = new Node(false);
            s->children.push_back(root);
            root = s;
            splitChild(s, 0);
        }
        insertNonFull(root, key, row);
    }

    std::optional<Row> Search(const Key& key) const {
        const Entry* e = searchNode(root, key);
        if (!e) return std::nullopt;
        return e->row;
    }

    std::vector<Entry> Scan() const {
        std::vector<Entry> out;
        inorder(root, out);
        return out;
    }

    void Print() const { printTree(root, 0); }
};

int main() {
    // minimum degree t = 2  ->  each node holds 1..3 entries (a 2-3-4 tree)
    DB<int, std::string> index(2);

    const std::pair<int, std::string> rows[] = {
        {10, "ROW_OF_10"}, {20, "ROW_OF_20"}, {5, "ROW_OF_5"},
        {6, "ROW_OF_6"},   {12, "ROW_OF_12"}, {30, "ROW_OF_30"},
        {7, "ROW_OF_7"},   {17, "ROW_OF_17"}, {40, "ROW_OF_40"},
        {3, "ROW_OF_3"},   {25, "ROW_OF_25"},
    };
    for (const auto& [k, v] : rows) index.Insert(k, v);

    std::cout << "=== B-Tree structure (one line per node) ===\n";
    index.Print();

    std::cout << "\n=== Point lookups ===\n";
    for (int key : {6, 17, 40, 99}) {
        auto r = index.Search(key);
        std::cout << "  Search(" << key << ") = "
                  << (r ? *r : std::string("<not found>")) << "\n";
    }

    std::cout << "\n=== Ordered scan (inorder traversal) ===\n  ";
    for (const auto& e : index.Scan()) std::cout << e.key << " ";
    std::cout << "\n";
    return 0;
}
