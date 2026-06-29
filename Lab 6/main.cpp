#include "BTree.h"
#include <iostream>
#include <vector>
#include <string>

void printHeader(const std::string& title) {
    std::cout << "\n======================================================================\n";
    std::cout << " " << title << "\n";
    std::cout << "======================================================================\n";
}

int main() {
    std::cout << "======================================================================\n";
    std::cout << " LAB 6: B-TREE INDEX IMPLEMENTATION\n";
    std::cout << "======================================================================\n";

    // Task 1: B-Tree Initialization
    printHeader("TASK 1: B-TREE INITIALIZATION");
    int degree = 3; // t = 3
    std::cout << "[Action] Initializing B-Tree with degree t = " << degree << "...\n";
    BTree tree(degree);
    tree.printMetadata();

    // Task 2: Record Insertion
    printHeader("TASK 2: RECORD INSERTION");
    std::cout << "[Action] Inserting 5 keys to fill up the initial root node (max keys = 2t - 1 = 5)...\n";
    tree.insert(10, "Record_Ten");
    tree.insert(20, "Record_Twenty");
    tree.insert(30, "Record_Thirty");
    tree.insert(40, "Record_Forty");
    tree.insert(50, "Record_Fifty");
    
    std::cout << "\n[Observation] Current Tree Structure after inserting 10, 20, 30, 40, 50:\n";
    tree.printTree();
    std::cout << "\nAnalysis: Keys are stored in sorted order. Since max keys is 5, the root is now full.\n";

    // Task 3: Node Splitting
    printHeader("TASK 3: NODE SPLITTING");
    std::cout << "[Action] Inserting key 60 to trigger a root node split...\n";
    tree.insert(60, "Record_Sixty");
    
    std::cout << "\n[Observation] Current Tree Structure after insert of 60:\n";
    tree.printTree();
    std::cout << "\nAnalysis:\n";
    std::cout << " - The root was full (5 keys). Inserting 60 triggered a root split.\n";
    std::cout << " - The median key 30 was selected and promoted to become the new root.\n";
    std::cout << " - Two new child nodes were created: Left containing [10, 20] and Right containing [40, 50, 60].\n";
    std::cout << " - The tree height increased from 1 to 2.\n";

    // Task 4: Search Operations
    printHeader("TASK 4: SEARCH OPERATIONS");
    auto runSearch = [&](int key) {
        std::vector<const BTreeNode*> path;
        std::cout << "[Action] Searching for key: " << key << "\n";
        auto [value, found] = tree.search(key, path);
        if (found) {
            std::cout << "  - Result: FOUND! Value = \"" << value << "\"\n";
        } else {
            std::cout << "  - Result: NOT FOUND!\n";
        }
        std::cout << "  - Traversal Path: ";
        for (size_t i = 0; i < path.size(); ++i) {
            std::cout << "Node" << i << " [";
            for (size_t k = 0; k < path[i]->keys.size(); ++k) {
                std::cout << path[i]->keys[k];
                if (k + 1 < path[i]->keys.size()) std::cout << ", ";
            }
            std::cout << "]";
            if (i + 1 < path.size()) std::cout << " -> ";
        }
        std::cout << "\n  - Number of Node Accesses: " << path.size() << "\n\n";
    };

    runSearch(30);  // Root node match
    runSearch(50);  // Traverses to right child
    runSearch(99);  // Non-existing key traversal

    // Task 5: Tree Structure Analysis
    printHeader("TASK 5: TREE STRUCTURE ANALYSIS");
    std::cout << "[Action] Inserting more keys to build a more complex structure (70, 80, 90, 15, 25, 5)...\n";
    tree.insert(70, "Record_Seventy");
    tree.insert(80, "Record_Eighty");
    tree.insert(90, "Record_Ninety");
    tree.insert(15, "Record_Fifteen");
    tree.insert(25, "Record_TwentyFive");
    tree.insert(5,  "Record_Five");

    std::cout << "\n[Observation] Final Tree Structure:\n";
    tree.printTree();

    std::cout << "\nDetailed Tree Analysis:\n";
    std::cout << " 1. Depth Balance: Notice how all leaf nodes are at the exact same depth (2 children-levels deep).\n";
    std::cout << " 2. Key Distribution: Each node maintains at least (t-1) = 2 keys (except root) and at most (2t-1) = 5 keys.\n";
    std::cout << " 3. Multi-way Branching: A B-Tree reduces the height of the tree significantly compared to a BST.\n";

    // Task 6: Indexing Behavior
    printHeader("TASK 6: INDEXING BEHAVIOR");
    std::cout << "Observations related to Database Indexing Concepts:\n";
    std::cout << " 1. Reduced Search Space: At each node, we do an ordered binary/linear search of keys to select the child pointer.\n";
    std::cout << "    This significantly reduces the search space. Instead of log2 steps, a B-Tree does log_t steps.\n";
    std::cout << " 2. Disk I/O Minimization: By setting 't' to a large number (typically matching block/page sizes on disk, e.g., 512 or 1024),\n";
    std::cout << "    a node fits exactly on a database block. This allows retrieving thousands of keys with a single disk I/O.\n";
    std::cout << " 3. Order Maintenance: The keys are stored sequentially in sorted order in the nodes, which facilitates efficient range queries.\n";

    std::cout << "\n======================================================================\n";
    std::cout << " LAB 6 COMPLETE\n";
    std::cout << "======================================================================\n";

    return 0;
}
