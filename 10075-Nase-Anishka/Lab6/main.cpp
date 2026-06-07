/*
 * Lab 6 — B-Tree Index Implementation
 * Roll No: 10075   |   Name: Nase Anishka
 *
 * A templated B-tree used as a key -> value INDEX (the value stands in for a
 * row / record pointer, the way a real database index maps a key to a tuple
 * location). The tree is parameterised by a "minimum degree" t (t >= 2):
 *
 *     - every node stores at most  2t - 1  keys  and  2t  children,
 *     - every node except the root stores at least  t - 1  keys,
 *     - all leaves sit at the same depth (this is what keeps it balanced).
 *
 * Design choices that make this mine rather than a textbook copy:
 *   - each node keeps keys and values in two PARALLEL vectors (keys[] / vals[]),
 *     mirroring how a real page separates the sort key from the payload;
 *   - insertion is proactive / top-down (CLRS style): a full child is split
 *     BEFORE we descend into it, so a single root-to-leaf pass suffices and we
 *     never have to walk back up;
 *   - validate() actually checks every B-tree invariant and throws if one is
 *     broken — including the "all leaves at the same depth" rule, which is the
 *     one a buggy split would silently violate;
 *   - findTraced() narrates the search path and counts node accesses, so the
 *     "reduced search space at each level" idea is visible, not just claimed.
 *
 * Build:  cmake -B build -S . && cmake --build build   (then ./build/btree_index)
 *   or:   g++ -std=c++17 -Wall -Wextra -Wpedantic -o btree_index main.cpp
 */

#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <stdexcept>
#include <cstddef>

template <typename Key, typename Value>
class BTreeIndex {
public:
    explicit BTreeIndex(int minDegree) : t(minDegree) {
        if (minDegree < 2)
            throw std::invalid_argument("B-tree minimum degree must be >= 2");
        root = new Node(true);
    }
    ~BTreeIndex() { destroy(root); }

    // Owns raw child pointers — copying would double-free, so forbid it.
    BTreeIndex(const BTreeIndex&)            = delete;
    BTreeIndex& operator=(const BTreeIndex&) = delete;

    // Capacity limits implied by the minimum degree.
    int minKeys()     const { return t - 1; }
    int maxKeys()     const { return 2 * t - 1; }
    int maxChildren() const { return 2 * t; }

    // ---- public index operations ----
    void         insert(const Key& key, const Value& value);   // insert or update
    const Value* find(const Key& key)   const;                 // nullptr if absent
    const Value* findTraced(const Key& key) const;             // same, but narrates

    // ---- introspection ----
    std::size_t size()      const { return keyCount; }
    int         height()    const { return depthOf(root); }    // edges root->leaf
    std::size_t nodeCount() const { return countNodes(root); }

    void printStructure() const { std::cout << "structure (indent = depth):\n";
                                  printStructure(root, 0); }
    void printLevels()    const;                               // BFS, one row per level
    void inorder()        const { std::cout << "in-order keys:"; inorder(root);
                                  std::cout << '\n'; }
    void validate()       const;                               // throws on any violation

    void setTrace(bool on) { trace = on; }

private:
    struct Node {
        bool               leaf;
        std::vector<Key>   keys;
        std::vector<Value> vals;   // vals[i] is the payload for keys[i]
        std::vector<Node*> kids;   // size == keys.size()+1 on an internal node
        explicit Node(bool isLeaf) : leaf(isLeaf) {}
    };

    int         t;            // minimum degree
    Node*       root;
    std::size_t keyCount = 0;
    bool        trace    = false;

    bool nodeFull(const Node* n) const { return static_cast<int>(n->keys.size()) == maxKeys(); }

    // First index i with keys[i] >= key  (the slot to land on or descend through).
    // Uses only operator< so any strictly-ordered Key type works.
    static int seek(const Node* n, const Key& key) {
        int i = 0;
        while (i < static_cast<int>(n->keys.size()) && n->keys[i] < key) ++i;
        return i;
    }
    static bool equalKeys(const Key& a, const Key& b) { return !(a < b) && !(b < a); }

    template <typename T> static std::string str(const T& v) {
        std::ostringstream os; os << v; return os.str();
    }
    static std::string joinKeys(const Node* n) {
        std::string s = "[";
        for (std::size_t i = 0; i < n->keys.size(); ++i) {
            if (i) s += " | ";
            s += str(n->keys[i]);
        }
        return s + "]";
    }

    Node* locate(const Key& key) const;                        // node holding key, or nullptr
    void  splitChild(Node* parent, int ci);
    void  insertNonFull(Node* n, const Key& key, const Value& value);

    void        destroy(Node* n);
    int         depthOf(const Node* n) const;
    std::size_t countNodes(const Node* n) const;
    void        printStructure(const Node* n, int level) const;
    void        inorder(const Node* n) const;
    int         validate(const Node* n, bool isRoot,
                         const Key* lo, const Key* hi) const;   // returns leaf depth
};

// ----------------------------------------------------------------------------
// search
// ----------------------------------------------------------------------------
template <typename Key, typename Value>
typename BTreeIndex<Key, Value>::Node*
BTreeIndex<Key, Value>::locate(const Key& key) const {
    Node* n = root;
    while (n) {
        int i = seek(n, key);
        if (i < static_cast<int>(n->keys.size()) && equalKeys(key, n->keys[i]))
            return n;                       // found at this node, slot i
        if (n->leaf) return nullptr;        // ran out of tree
        n = n->kids[i];                     // descend into the gap
    }
    return nullptr;
}

template <typename Key, typename Value>
const Value* BTreeIndex<Key, Value>::find(const Key& key) const {
    Node* n = locate(key);
    if (!n) return nullptr;
    return &n->vals[seek(n, key)];
}

template <typename Key, typename Value>
const Value* BTreeIndex<Key, Value>::findTraced(const Key& key) const {
    std::cout << "  search " << key << ": ";
    Node* n = root;
    int accesses = 0;
    while (n) {
        ++accesses;
        int i = seek(n, key);
        std::cout << joinKeys(n);
        if (i < static_cast<int>(n->keys.size()) && equalKeys(key, n->keys[i])) {
            std::cout << "  -> HIT (" << accesses << " node access"
                      << (accesses == 1 ? "" : "es") << ")\n";
            return &n->vals[i];
        }
        if (n->leaf) {
            std::cout << "  -> MISS (" << accesses << " node access"
                      << (accesses == 1 ? "" : "es") << ")\n";
            return nullptr;
        }
        std::cout << " -> child " << i << "  ";
        n = n->kids[i];
    }
    return nullptr;
}

// ----------------------------------------------------------------------------
// insert (proactive top-down split)
// ----------------------------------------------------------------------------
template <typename Key, typename Value>
void BTreeIndex<Key, Value>::splitChild(Node* parent, int ci) {
    Node* full  = parent->kids[ci];
    Node* right = new Node(full->leaf);
    const int mid = t - 1;                  // median index inside the full node

    // Upper t-1 keys/vals move to the new right sibling.
    right->keys.assign(full->keys.begin() + t, full->keys.end());
    right->vals.assign(full->vals.begin() + t, full->vals.end());

    Key   medKey = full->keys[mid];         // median is promoted into the parent
    Value medVal = full->vals[mid];

    if (!full->leaf) {                       // upper t children follow the keys
        right->kids.assign(full->kids.begin() + t, full->kids.end());
        full->kids.resize(t);
    }
    full->keys.resize(mid);                  // left keeps the lower t-1 keys/vals
    full->vals.resize(mid);

    parent->kids.insert(parent->kids.begin() + ci + 1, right);
    parent->keys.insert(parent->keys.begin() + ci,     medKey);
    parent->vals.insert(parent->vals.begin() + ci,     medVal);

    if (trace)
        std::cout << "    split -> " << joinKeys(full) << "  ^" << medKey
                  << "^  " << joinKeys(right) << '\n';
}

template <typename Key, typename Value>
void BTreeIndex<Key, Value>::insertNonFull(Node* n, const Key& key, const Value& value) {
    int i = static_cast<int>(n->keys.size()) - 1;
    if (n->leaf) {
        // Shift larger keys right by one, then drop the newcomer into the gap.
        n->keys.push_back(key);
        n->vals.push_back(value);
        while (i >= 0 && key < n->keys[i]) {
            n->keys[i + 1] = n->keys[i];
            n->vals[i + 1] = n->vals[i];
            --i;
        }
        n->keys[i + 1] = key;
        n->vals[i + 1] = value;
    } else {
        while (i >= 0 && key < n->keys[i]) --i;
        ++i;                                 // child index to descend into
        if (nodeFull(n->kids[i])) {          // pre-split so the child has room
            splitChild(n, i);
            if (n->keys[i] < key) ++i;       // median rose up; go right of it
        }
        insertNonFull(n->kids[i], key, value);
    }
}

template <typename Key, typename Value>
void BTreeIndex<Key, Value>::insert(const Key& key, const Value& value) {
    if (Node* hit = locate(key)) {           // key already present -> update payload
        hit->vals[seek(hit, key)] = value;
        return;
    }
    if (nodeFull(root)) {                     // grow upward: new root over old
        Node* fresh = new Node(false);
        fresh->kids.push_back(root);
        root = fresh;
        splitChild(root, 0);                  // old root becomes two half-full kids
    }
    insertNonFull(root, key, value);
    ++keyCount;
}

// ----------------------------------------------------------------------------
// traversal / introspection
// ----------------------------------------------------------------------------
template <typename Key, typename Value>
void BTreeIndex<Key, Value>::inorder(const Node* n) const {
    for (std::size_t i = 0; i < n->keys.size(); ++i) {
        if (!n->leaf) inorder(n->kids[i]);
        std::cout << ' ' << n->keys[i];
    }
    if (!n->leaf) inorder(n->kids[n->keys.size()]);
}

template <typename Key, typename Value>
void BTreeIndex<Key, Value>::printStructure(const Node* n, int level) const {
    std::cout << std::string(static_cast<std::size_t>(level) * 4, ' ')
              << joinKeys(n) << (n->leaf ? "  (leaf)" : "") << '\n';
    for (Node* c : n->kids) printStructure(c, level + 1);
}

template <typename Key, typename Value>
void BTreeIndex<Key, Value>::printLevels() const {
    std::cout << "levels (each row is one depth; all leaves share the last row):\n";
    std::vector<Node*> cur{root};
    int level = 0;
    while (!cur.empty()) {
        std::vector<Node*> next;
        std::cout << "  L" << level << ":";
        for (Node* n : cur) {
            std::cout << ' ' << joinKeys(n);
            for (Node* c : n->kids) next.push_back(c);
        }
        std::cout << '\n';
        cur.swap(next);
        ++level;
    }
}

template <typename Key, typename Value>
int BTreeIndex<Key, Value>::depthOf(const Node* n) const {
    int d = 0;
    while (!n->leaf) { n = n->kids[0]; ++d; }   // balanced, so any path works
    return d;
}

template <typename Key, typename Value>
std::size_t BTreeIndex<Key, Value>::countNodes(const Node* n) const {
    std::size_t total = 1;
    for (Node* c : n->kids) total += countNodes(c);
    return total;
}

template <typename Key, typename Value>
void BTreeIndex<Key, Value>::destroy(Node* n) {
    if (!n) return;
    for (Node* c : n->kids) destroy(c);
    delete n;
}

// ----------------------------------------------------------------------------
// validate — throws std::logic_error naming the first broken invariant
// ----------------------------------------------------------------------------
template <typename Key, typename Value>
int BTreeIndex<Key, Value>::validate(const Node* n, bool isRoot,
                                     const Key* lo, const Key* hi) const {
    const int k = static_cast<int>(n->keys.size());

    if (n->vals.size() != n->keys.size())
        throw std::logic_error("keys[] and vals[] lengths disagree");
    if (k > maxKeys())
        throw std::logic_error("node has more than 2t-1 keys");
    if (!isRoot && k < minKeys())
        throw std::logic_error("non-root node has fewer than t-1 keys");

    // keys strictly sorted and inside the (lo, hi) window inherited from parent
    for (int i = 0; i < k; ++i) {
        if (i && !(n->keys[i - 1] < n->keys[i]))
            throw std::logic_error("keys within a node are not strictly sorted");
        if (lo && n->keys[i] < *lo)
            throw std::logic_error("key smaller than its parent separator");
        if (hi && *hi < n->keys[i])
            throw std::logic_error("key larger than its parent separator");
    }

    if (n->leaf) {
        if (!n->kids.empty())
            throw std::logic_error("a leaf must have no children");
        return 0;                                  // leaf depth contribution
    }
    if (static_cast<int>(n->kids.size()) != k + 1)
        throw std::logic_error("internal node child count != keys + 1");

    int leafDepth = -1;
    for (int i = 0; i <= k; ++i) {
        const Key* clo = (i == 0) ? lo : &n->keys[i - 1];
        const Key* chi = (i == k) ? hi : &n->keys[i];
        int d = validate(n->kids[i], false, clo, chi);
        if (leafDepth == -1) leafDepth = d;
        else if (leafDepth != d)
            throw std::logic_error("leaves are not all at the same depth");
    }
    return leafDepth + 1;
}

template <typename Key, typename Value>
void BTreeIndex<Key, Value>::validate() const {
    if (root->keys.empty() && root->leaf) return;  // empty tree is trivially valid
    validate(root, /*isRoot=*/true, nullptr, nullptr);
}

// ============================================================================
// driver
// ============================================================================
static void rule(const std::string& title) {
    std::cout << "\n=== " << title << " ===\n";
}

int main() {
    std::cout << "B-Tree Index demo (roll 10075)\n";

    // ---- Scenario 1: build a t=3 index and watch the splits happen --------
    rule("build with minimum degree t = 3");
    BTreeIndex<int, std::string> idx(3);
    std::cout << "each node holds " << idx.minKeys() << ".." << idx.maxKeys()
              << " keys and up to " << idx.maxChildren() << " children\n\n";

    const std::vector<std::pair<int, std::string>> data = {
        {50, "row#50"}, {30, "row#30"}, {70, "row#70"}, {20, "row#20"},
        {40, "row#40"}, {60, "row#60"}, {80, "row#80"}, {10, "row#10"},
        {25, "row#25"}, {35, "row#35"}, {45, "row#45"}, {55, "row#55"},
    };
    idx.setTrace(true);
    for (const auto& kv : data) {
        std::cout << "insert " << kv.first << '\n';
        idx.insert(kv.first, kv.second);
    }
    idx.setTrace(false);

    std::cout << '\n';
    idx.printStructure();
    std::cout << '\n';
    idx.printLevels();
    std::cout << '\n';
    idx.inorder();
    std::cout << "size=" << idx.size() << "  height=" << idx.height()
              << "  nodes=" << idx.nodeCount() << '\n';
    idx.validate();
    std::cout << "validate(): OK — all B-tree invariants hold\n";

    // ---- Scenario 2: traced searches (hit and miss) -----------------------
    rule("search: path walked + node accesses");
    idx.findTraced(45);   // exists, in a leaf
    idx.findTraced(50);   // exists, likely up in an internal node
    idx.findTraced(99);   // absent

    // ---- Scenario 3: sorted input is the BST worst case, not the B-tree's --
    rule("ascending insert 1..20 into a t=2 (2-3-4) tree");
    BTreeIndex<int, int> seq(2);
    for (int i = 1; i <= 20; ++i) seq.insert(i, i * i);   // value = i^2
    seq.printLevels();
    seq.inorder();
    std::cout << "size=" << seq.size() << "  height=" << seq.height()
              << "  nodes=" << seq.nodeCount()
              << "   (a plain BST on 1..20 would be a height-19 stick)\n";
    seq.validate();
    std::cout << "validate(): OK — every leaf sits at the same depth\n";

    // ---- Scenario 4: re-inserting a key updates the payload, not the shape -
    rule("upsert: insert(40, ...) again");
    std::cout << "before: 40 -> " << *idx.find(40) << ", size=" << idx.size() << '\n';
    idx.insert(40, "row#40-UPDATED");
    std::cout << "after : 40 -> " << *idx.find(40) << ", size=" << idx.size()
              << "  (size unchanged — it was an update)\n";
    idx.validate();

    return 0;
}
