// BTree.cpp
// A complete B-Tree of configurable minimum degree t.
//
// A B-Tree is a balanced multi-way search tree. Unlike a binary search tree,
// each node may hold up to 2t-1 keys and have up to 2t children, where t is the
// "minimum degree". This high fan-out keeps the tree shallow, which is exactly
// what makes B-Trees the workhorse of on-disk database indexes: a shallow tree
// means very few node visits (and therefore very few disk seeks) per lookup.
//
// Invariants maintained at all times:
//   * Every node other than the root holds between t-1 and 2t-1 keys.
//   * The root holds between 1 and 2t-1 keys (or is empty when the tree is empty).
//   * A node with c keys has exactly c+1 children if it is internal, 0 if a leaf.
//   * Keys within a node are sorted; the i-th child subtree holds keys between
//     key[i-1] and key[i].
//   * ALL leaves sit at the same depth. The tree only grows/shrinks at the root.
//
// Build: g++ -std=c++17 BTree.cpp -o btree

#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>

// ---------------------------------------------------------------------------
// Node
// ---------------------------------------------------------------------------
// A node owns its keys and (if internal) pointers to its children. We keep the
// node logic minimal and drive most of the work from the BTree class so the
// insert/delete algorithms read top-to-bottom in one place.
struct BTreeNode {
    std::vector<int> keys;              // sorted keys held by this node
    std::vector<BTreeNode*> children;   // child pointers; empty when leaf
    bool leaf;                          // true if this node has no children

    explicit BTreeNode(bool isLeaf) : leaf(isLeaf) {}

    // Recursively free children. The BTree destructor starts the cascade.
    ~BTreeNode() {
        for (BTreeNode* c : children) delete c;
    }
};

// ---------------------------------------------------------------------------
// BTree
// ---------------------------------------------------------------------------
class BTree {
public:
    explicit BTree(int minDegree) : t(minDegree), root(nullptr) {
        // A B-Tree of minimum degree t must satisfy t >= 2; t = 1 would allow
        // nodes with zero keys, which breaks the structure.
        if (t < 2) t = 2;
    }

    ~BTree() { delete root; }

    // Non-copyable: the tree owns raw pointers and a shallow copy would
    // double-free. (Not needed for the demo, but it documents ownership.)
    BTree(const BTree&) = delete;
    BTree& operator=(const BTree&) = delete;

    // -------------------- public API --------------------
    void insert(int key);
    bool search(int key) const;
    void remove(int key);

    void printLevelOrder() const;   // breadth-first, one tree level per line
    void printInOrder() const;      // sorted listing of all keys

private:
    int t;             // minimum degree
    BTreeNode* root;

    // insert helpers
    void splitChild(BTreeNode* parent, int childIndex);
    void insertNonFull(BTreeNode* node, int key);

    // search helper
    bool search(BTreeNode* node, int key) const;

    // delete helpers
    void removeFromNode(BTreeNode* node, int key);
    void removeFromLeaf(BTreeNode* node, int idx);
    void removeFromInternal(BTreeNode* node, int idx);
    int  getPredecessor(BTreeNode* node, int idx) const;
    int  getSuccessor(BTreeNode* node, int idx) const;
    void fill(BTreeNode* node, int idx);          // ensure child[idx] has >= t keys
    void borrowFromPrev(BTreeNode* node, int idx);
    void borrowFromNext(BTreeNode* node, int idx);
    void merge(BTreeNode* node, int idx);         // merge child[idx] and child[idx+1]

    // traversal helper
    void inOrder(BTreeNode* node, std::vector<int>& out) const;
};

// ===========================================================================
// SEARCH
// ===========================================================================
bool BTree::search(int key) const {
    return root ? search(root, key) : false;
}

bool BTree::search(BTreeNode* node, int key) const {
    // Scan keys until we reach one >= the target. Within a node a linear scan
    // is fine; on real disk-backed trees this would be a binary search inside
    // the loaded block.
    int i = 0;
    while (i < (int)node->keys.size() && key > node->keys[i]) i++;

    if (i < (int)node->keys.size() && node->keys[i] == key) return true; // found
    if (node->leaf) return false;                                        // dead end
    return search(node->children[i], key);                               // descend
}

// ===========================================================================
// INSERT  (top-down, proactive split)
// ===========================================================================
// We split any full node we encounter on the way down. Because the parent is
// guaranteed non-full when we split a child, the median can always move up
// without overflowing the parent. This keeps insertion a single downward pass.
void BTree::insert(int key) {
    if (root == nullptr) {
        root = new BTreeNode(/*leaf=*/true);
        root->keys.push_back(key);
        return;
    }

    // If the root is full we must grow the tree in height first. A fresh root
    // is created, the old root becomes its only child, and we split it. This is
    // the *only* place the tree height increases.
    if ((int)root->keys.size() == 2 * t - 1) {
        BTreeNode* newRoot = new BTreeNode(/*leaf=*/false);
        newRoot->children.push_back(root);
        splitChild(newRoot, 0);
        root = newRoot;
    }
    insertNonFull(root, key);
}

// Split the full child at parent->children[childIndex] around its median.
// Before: child has 2t-1 keys.
// After : child keeps the first t-1 keys, a new sibling takes the last t-1,
//         and the median (child->keys[t-1]) is lifted into the parent.
void BTree::splitChild(BTreeNode* parent, int childIndex) {
    BTreeNode* full = parent->children[childIndex];
    BTreeNode* sibling = new BTreeNode(full->leaf);

    int median = full->keys[t - 1];

    // Move the upper t-1 keys into the new sibling.
    sibling->keys.assign(full->keys.begin() + t, full->keys.end());
    // Move the upper t child pointers too, if internal.
    if (!full->leaf) {
        sibling->children.assign(full->children.begin() + t, full->children.end());
        full->children.resize(t);
    }
    // Shrink the original node down to its lower t-1 keys.
    full->keys.resize(t - 1);

    // Link the new sibling into the parent just after the full child, and
    // insert the median key in the matching slot.
    parent->children.insert(parent->children.begin() + childIndex + 1, sibling);
    parent->keys.insert(parent->keys.begin() + childIndex, median);
}

// Insert into a node that is guaranteed not to be full.
void BTree::insertNonFull(BTreeNode* node, int key) {
    int i = (int)node->keys.size() - 1;

    if (node->leaf) {
        // Find the insertion point and place the key, keeping keys sorted.
        node->keys.push_back(0); // make room
        while (i >= 0 && key < node->keys[i]) {
            node->keys[i + 1] = node->keys[i];
            i--;
        }
        node->keys[i + 1] = key;
        return;
    }

    // Internal node: find the child that should receive the key.
    while (i >= 0 && key < node->keys[i]) i--;
    i++; // i is now the index of the target child

    // Proactively split the target child if it is full, so the recursive call
    // always lands in a non-full node.
    if ((int)node->children[i]->keys.size() == 2 * t - 1) {
        splitChild(node, i);
        // After the split the median rose into node->keys[i]; decide which of
        // the two halves the key belongs to.
        if (key > node->keys[i]) i++;
    }
    insertNonFull(node->children[i], key);
}

// ===========================================================================
// DELETE
// ===========================================================================
// The deletion algorithm follows the classic CLRS approach. The key idea is to
// guarantee, before descending into a child, that the child has at least t keys
// (i.e. at least one more than the minimum t-1). That way a delete in the
// subtree can never underflow the child without us being able to fix it from
// above. We achieve this by borrowing from a sibling or merging.
void BTree::remove(int key) {
    if (root == nullptr) return;

    removeFromNode(root, key);

    // If the root lost its last key, shrink the tree by one level: its only
    // child becomes the new root (or the tree becomes empty).
    if (root->keys.empty()) {
        BTreeNode* old = root;
        root = root->leaf ? nullptr : root->children[0];
        old->children.clear();   // detach so ~BTreeNode does not free the new root
        delete old;
    }
}

// Remove `key` from the subtree rooted at `node`. Precondition: `node` has at
// least t keys, unless it is the root.
void BTree::removeFromNode(BTreeNode* node, int key) {
    // Locate the first index idx with keys[idx] >= key.
    int idx = 0;
    while (idx < (int)node->keys.size() && node->keys[idx] < key) idx++;

    if (idx < (int)node->keys.size() && node->keys[idx] == key) {
        // Key is in this node.
        if (node->leaf) removeFromLeaf(node, idx);
        else            removeFromInternal(node, idx);
        return;
    }

    // Key is not in this node.
    if (node->leaf) return; // not present anywhere

    // Does the key live in the last child? (idx == number of keys)
    bool inLastChild = (idx == (int)node->keys.size());

    // Make sure the child we are about to descend into has at least t keys.
    if ((int)node->children[idx]->keys.size() < t) fill(node, idx);

    // fill() may have merged child[idx] into child[idx-1], reducing the child
    // count. In that case the key, if it was in the (former) last child, now
    // lives in child[idx-1].
    if (inLastChild && idx > (int)node->keys.size())
        removeFromNode(node->children[idx - 1], key);
    else
        removeFromNode(node->children[idx], key);
}

// Simplest case: key is in a leaf. Just erase it. The precondition guarantees
// the leaf has enough keys to remain valid (or it is the root).
void BTree::removeFromLeaf(BTreeNode* node, int idx) {
    node->keys.erase(node->keys.begin() + idx);
}

// Key is in an internal node at position idx.
void BTree::removeFromInternal(BTreeNode* node, int idx) {
    int key = node->keys[idx];

    if ((int)node->children[idx]->keys.size() >= t) {
        // Left child is fat enough: replace key with its predecessor, then
        // delete the predecessor from the left subtree.
        int pred = getPredecessor(node, idx);
        node->keys[idx] = pred;
        removeFromNode(node->children[idx], pred);
    } else if ((int)node->children[idx + 1]->keys.size() >= t) {
        // Right child is fat enough: use the successor symmetrically.
        int succ = getSuccessor(node, idx);
        node->keys[idx] = succ;
        removeFromNode(node->children[idx + 1], succ);
    } else {
        // Both neighbouring children are minimal (t-1 keys). Merge the key and
        // the right child into the left child, then delete from the merged node
        // which now safely holds 2t-1 keys.
        merge(node, idx);
        removeFromNode(node->children[idx], key);
    }
}

// Largest key in the subtree to the left of keys[idx] (rightmost leaf key).
int BTree::getPredecessor(BTreeNode* node, int idx) const {
    BTreeNode* cur = node->children[idx];
    while (!cur->leaf) cur = cur->children.back();
    return cur->keys.back();
}

// Smallest key in the subtree to the right of keys[idx] (leftmost leaf key).
int BTree::getSuccessor(BTreeNode* node, int idx) const {
    BTreeNode* cur = node->children[idx + 1];
    while (!cur->leaf) cur = cur->children.front();
    return cur->keys.front();
}

// Ensure node->children[idx] has at least t keys, by borrowing from a sibling
// or merging. Called before descending so the recursion's precondition holds.
void BTree::fill(BTreeNode* node, int idx) {
    // Prefer borrowing from the left sibling if it can spare a key.
    if (idx != 0 && (int)node->children[idx - 1]->keys.size() >= t) {
        borrowFromPrev(node, idx);
    }
    // Otherwise borrow from the right sibling if it can spare a key.
    else if (idx != (int)node->keys.size() &&
             (int)node->children[idx + 1]->keys.size() >= t) {
        borrowFromNext(node, idx);
    }
    // Neither sibling can spare a key: merge with one of them.
    else {
        if (idx != (int)node->keys.size()) merge(node, idx);       // merge with right
        else                               merge(node, idx - 1);   // merge with left
    }
}

// Rotate a key right: take the parent's separating key down into child[idx] and
// pull the left sibling's last key up into the parent. The left sibling's last
// child pointer moves to the front of child[idx].
void BTree::borrowFromPrev(BTreeNode* node, int idx) {
    BTreeNode* child = node->children[idx];
    BTreeNode* sibling = node->children[idx - 1];

    // Separator key from parent drops to the front of child.
    child->keys.insert(child->keys.begin(), node->keys[idx - 1]);
    // Sibling's largest key rises to become the new separator.
    node->keys[idx - 1] = sibling->keys.back();
    sibling->keys.pop_back();

    // Move the sibling's last child to the front of child, if internal.
    if (!child->leaf) {
        child->children.insert(child->children.begin(), sibling->children.back());
        sibling->children.pop_back();
    }
}

// Mirror of borrowFromPrev: rotate a key left from the right sibling.
void BTree::borrowFromNext(BTreeNode* node, int idx) {
    BTreeNode* child = node->children[idx];
    BTreeNode* sibling = node->children[idx + 1];

    // Separator key from parent drops to the back of child.
    child->keys.push_back(node->keys[idx]);
    // Sibling's smallest key rises to become the new separator.
    node->keys[idx] = sibling->keys.front();
    sibling->keys.erase(sibling->keys.begin());

    // Move the sibling's first child to the back of child, if internal.
    if (!child->leaf) {
        child->children.push_back(sibling->children.front());
        sibling->children.erase(sibling->children.begin());
    }
}

// Merge child[idx], the separating key keys[idx], and child[idx+1] into a
// single node (stored at child[idx]). The result holds (t-1) + 1 + (t-1) = 2t-1
// keys, exactly the maximum, so it stays valid. child[idx+1] is then removed.
void BTree::merge(BTreeNode* node, int idx) {
    BTreeNode* left = node->children[idx];
    BTreeNode* right = node->children[idx + 1];

    // Pull the separating key down into the left node.
    left->keys.push_back(node->keys[idx]);
    // Append all of the right node's keys and children.
    left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
    if (!left->leaf) {
        left->children.insert(left->children.end(),
                              right->children.begin(), right->children.end());
    }

    // Remove the separator and the now-empty right child slot from the parent.
    node->keys.erase(node->keys.begin() + idx);
    node->children.erase(node->children.begin() + idx + 1);

    // Detach the right node's children before freeing it (they were moved).
    right->children.clear();
    delete right;
}

// ===========================================================================
// TRAVERSAL / PRINTING
// ===========================================================================
void BTree::inOrder(BTreeNode* node, std::vector<int>& out) const {
    if (!node) return;
    int i = 0;
    for (; i < (int)node->keys.size(); ++i) {
        if (!node->leaf) inOrder(node->children[i], out);
        out.push_back(node->keys[i]);
    }
    if (!node->leaf) inOrder(node->children[i], out); // last (rightmost) child
}

void BTree::printInOrder() const {
    std::vector<int> out;
    inOrder(root, out);
    std::cout << "In-order keys:";
    for (int k : out) std::cout << ' ' << k;
    std::cout << '\n';
}

// Breadth-first print: each line is one level of the tree, with keys of a node
// grouped inside [ ... ] and nodes separated by spaces. This makes splits,
// borrows, and merges easy to see.
void BTree::printLevelOrder() const {
    std::cout << "Level-order tree (t = " << t << "):\n";
    if (!root) { std::cout << "  <empty>\n"; return; }

    std::queue<BTreeNode*> level;
    level.push(root);
    int depth = 0;

    while (!level.empty()) {
        int count = (int)level.size();
        std::cout << "  L" << depth << ":";
        for (int n = 0; n < count; ++n) {
            BTreeNode* cur = level.front();
            level.pop();

            std::cout << " [";
            for (int i = 0; i < (int)cur->keys.size(); ++i) {
                if (i) std::cout << ' ';
                std::cout << cur->keys[i];
            }
            std::cout << ']';

            for (BTreeNode* c : cur->children) level.push(c);
        }
        std::cout << '\n';
        ++depth;
    }
}

// ===========================================================================
// DEMO
// ===========================================================================
int main() {
    // Minimum degree t = 3  =>  each node holds 2..5 keys (root may hold 1..5).
    BTree tree(3);

    std::cout << "=== Building the tree ===\n";
    int toInsert[] = {10, 20, 5, 6, 12, 30, 7, 17, 3, 8,
                      25, 40, 45, 50, 55, 60, 13, 14, 15, 16};
    for (int k : toInsert) tree.insert(k);

    tree.printLevelOrder();
    tree.printInOrder();

    std::cout << "\n=== Searches ===\n";
    for (int k : {6, 15, 99, 50}) {
        std::cout << "  search(" << k << ") -> "
                  << (tree.search(k) ? "found" : "not found") << '\n';
    }

    // ---- Delete from a leaf with no rebalancing needed ----
    std::cout << "\n=== delete(6)  [plain leaf delete] ===\n";
    tree.remove(6);
    tree.printLevelOrder();

    // ---- Delete that forces a BORROW from a sibling ----
    std::cout << "\n=== delete(13)  [may trigger borrow] ===\n";
    tree.remove(13);
    tree.printLevelOrder();

    // ---- Delete an INTERNAL key (predecessor/successor replacement) ----
    std::cout << "\n=== delete(20)  [internal node key] ===\n";
    tree.remove(20);
    tree.printLevelOrder();

    // ---- A run of deletes to force MERGES and a root shrink ----
    std::cout << "\n=== deleting 3, 5, 7, 8, 10, 12  [forces merges] ===\n";
    for (int k : {3, 5, 7, 8, 10, 12}) tree.remove(k);
    tree.printLevelOrder();
    tree.printInOrder();

    // ---- Delete everything else, confirming the tree empties cleanly ----
    std::cout << "\n=== deleting the remainder ===\n";
    for (int k : {14, 15, 16, 17, 25, 30, 40, 45, 50, 55, 60}) tree.remove(k);
    tree.printLevelOrder();
    tree.printInOrder();

    return 0;
}
