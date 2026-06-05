#include <iostream>
#include <string>
#include <vector>

// B-Tree of degree t means:
//   - every node has at most  2t-1 keys
//   - every non-root node has at least t-1 keys
//   - every non-leaf node has (keys+1) children

struct BTreeNode {
    std::vector<int>         keys;
    std::vector<std::string> values;   // one value per key
    std::vector<BTreeNode*>  children;
    bool                     isLeaf;

    BTreeNode(bool leaf) : isLeaf(leaf) {}
};

class BTree {
public:
    int       t;      // minimum degree
    BTreeNode* root;

    BTree(int degree) {
        t    = degree;
        root = new BTreeNode(true);
    }

    // ── INSERT ──────────────────────────────────────────────
    void insert(int key, std::string val) {
        // if root is full, split it first
        if ((int)root->keys.size() == 2 * t - 1) {
            std::cout << "  [split] root is full, splitting root\n";
            BTreeNode* newRoot = new BTreeNode(false);
            newRoot->children.push_back(root);
            splitChild(newRoot, 0, root);
            root = newRoot;
        }
        insertNonFull(root, key, val);
        std::cout << "  [insert] key=" << key << " val=\"" << val << "\"\n";
    }

    // ── SEARCH ──────────────────────────────────────────────
    std::string search(int key) {
        int nodeAccesses = 0;
        std::string result = searchHelper(root, key, nodeAccesses);
        if (result == "") {
            std::cout << "  [search] key=" << key
                      << " NOT FOUND | node accesses: " << nodeAccesses << "\n";
        } else {
            std::cout << "  [search] key=" << key
                      << " FOUND val=\"" << result
                      << "\" | node accesses: " << nodeAccesses << "\n";
        }
        return result;
    }

    // ── TRAVERSAL (inorder) ─────────────────────────────────
    void traverse() {
        std::cout << "  Traversal: ";
        traverseHelper(root);
        std::cout << "\n";
    }

    // ── PRINT TREE ──────────────────────────────────────────
    void printTree() {
        std::cout << "\n  B-Tree structure (degree t=" << t << "):\n";
        printHelper(root, 0);
    }

    // ── TREE STATS ──────────────────────────────────────────
    void printStats() {
        int depth  = 0;
        int nodes  = 0;
        int keys   = 0;
        statsHelper(root, 0, depth, nodes, keys);
        std::cout << "  Max depth  : " << depth  << "\n";
        std::cout << "  Node count : " << nodes  << "\n";
        std::cout << "  Key count  : " << keys   << "\n";
        std::cout << "  Max keys/node (2t-1) : " << 2*t-1 << "\n";
        std::cout << "  Min keys/node (t-1)  : " << t-1   << "\n";
    }

private:
    // ── SPLIT CHILD ─────────────────────────────────────────
    // splits parent->children[i] which is full (2t-1 keys)
    void splitChild(BTreeNode* parent, int i, BTreeNode* child) {
        BTreeNode* sibling = new BTreeNode(child->isLeaf);

        // median index
        int mid = t - 1;
        int medianKey = child->keys[mid];
        std::string medianVal = child->values[mid];

        std::cout << "  [split] node with keys [";
        for (int k = 0; k < (int)child->keys.size(); k++) {
            std::cout << child->keys[k];
            if (k < (int)child->keys.size()-1) std::cout << ",";
        }
        std::cout << "] → promote key=" << medianKey << "\n";

        // copy right half of child to sibling
        for (int j = mid + 1; j < (int)child->keys.size(); j++) {
            sibling->keys.push_back(child->keys[j]);
            sibling->values.push_back(child->values[j]);
        }
        if (!child->isLeaf) {
            for (int j = mid + 1; j < (int)child->children.size(); j++)
                sibling->children.push_back(child->children[j]);
        }

        // trim child to left half
        child->keys.resize(mid);
        child->values.resize(mid);
        if (!child->isLeaf)
            child->children.resize(mid + 1);

        // insert sibling and median into parent
        parent->children.insert(parent->children.begin() + i + 1, sibling);
        parent->keys.insert(parent->keys.begin() + i, medianKey);
        parent->values.insert(parent->values.begin() + i, medianVal);
    }

    // ── INSERT NON-FULL ─────────────────────────────────────
    void insertNonFull(BTreeNode* node, int key, std::string val) {
        int i = (int)node->keys.size() - 1;

        if (node->isLeaf) {
            // find right spot and shift
            node->keys.push_back(0);
            node->values.push_back("");
            while (i >= 0 && key < node->keys[i]) {
                node->keys[i+1]   = node->keys[i];
                node->values[i+1] = node->values[i];
                i--;
            }
            node->keys[i+1]   = key;
            node->values[i+1] = val;
        } else {
            // find which child to descend into
            while (i >= 0 && key < node->keys[i]) i--;
            i++;

            // split child if full before descending
            if ((int)node->children[i]->keys.size() == 2 * t - 1) {
                std::cout << "  [split] child at index " << i << " is full\n";
                splitChild(node, i, node->children[i]);
                if (key > node->keys[i]) i++;
            }
            insertNonFull(node->children[i], key, val);
        }
    }

    // ── SEARCH HELPER ───────────────────────────────────────
    std::string searchHelper(BTreeNode* node, int key, int& accesses) {
        if (node == nullptr) return "";
        accesses++;

        int i = 0;
        while (i < (int)node->keys.size() && key > node->keys[i]) i++;

        if (i < (int)node->keys.size() && key == node->keys[i])
            return node->values[i];

        if (node->isLeaf) return "";

        return searchHelper(node->children[i], key, accesses);
    }

    // ── TRAVERSAL HELPER ────────────────────────────────────
    void traverseHelper(BTreeNode* node) {
        if (node == nullptr) return;
        for (int i = 0; i < (int)node->keys.size(); i++) {
            if (!node->isLeaf)
                traverseHelper(node->children[i]);
            std::cout << node->keys[i] << " ";
        }
        if (!node->isLeaf)
            traverseHelper(node->children[node->keys.size()]);
    }

    // ── PRINT HELPER ────────────────────────────────────────
    void printHelper(BTreeNode* node, int depth) {
        if (node == nullptr) return;

        std::string indent(depth * 4, ' ');
        std::cout << indent << "[";
        for (int i = 0; i < (int)node->keys.size(); i++) {
            std::cout << node->keys[i];
            if (i < (int)node->keys.size()-1) std::cout << "|";
        }
        std::cout << "]" << (node->isLeaf ? " (leaf)" : "") << "\n";

        for (BTreeNode* child : node->children)
            printHelper(child, depth + 1);
    }

    // ── STATS HELPER ────────────────────────────────────────
    void statsHelper(BTreeNode* node, int curDepth, int& maxDepth, int& nodes, int& keys) {
        if (node == nullptr) return;
        nodes++;
        keys += node->keys.size();
        if (curDepth > maxDepth) maxDepth = curDepth;
        for (BTreeNode* child : node->children)
            statsHelper(child, curDepth + 1, maxDepth, nodes, keys);
    }
};

// ── MAIN ────────────────────────────────────────────────────
int main() {
    std::cout << "============================================\n"
              << "   B-Tree Index Implementation (degree t=3)\n"
              << "============================================\n\n";

    // Task 1: init with degree 3 (max 5 keys per node)
    std::cout << ">>> Task 1: Initialization\n\n";
    BTree bt(3);
    std::cout << "  Degree t      : " << bt.t        << "\n";
    std::cout << "  Max keys/node : " << 2*bt.t - 1  << "\n";
    std::cout << "  Min keys/node : " << bt.t - 1    << "\n\n";

    // Task 2 & 3: insertions + splits
    std::cout << ">>> Task 2 & 3: Insertions + Node Splitting\n\n";
    std::vector<std::pair<int,std::string>> records = {
        {10,"Alice"},{20,"Bob"},{5,"Charlie"},{6,"Diana"},
        {12,"Eve"},{30,"Frank"},{7,"Grace"},{17,"Heidi"},
        {3,"Ivan"},{8,"Judy"},{25,"Karl"},{35,"Liam"}
    };
    for (auto& [k, v] : records)
        bt.insert(k, v);

    // Task 4: search
    std::cout << "\n>>> Task 4: Search Operations\n\n";
    for (int key : {6, 25, 99, 3})
        bt.search(key);

    // Task 5: tree structure
    std::cout << "\n>>> Task 5: Tree Structure\n";
    bt.printTree();
    std::cout << "\n  Stats:\n";
    bt.printStats();

    // Task 6: traversal (inorder = sorted)
    std::cout << "\n>>> Task 6: Inorder Traversal (sorted keys)\n\n";
    bt.traverse();

    std::cout << "\n============================================\n"
              << "   Done\n"
              << "============================================\n";
    return 0;
}