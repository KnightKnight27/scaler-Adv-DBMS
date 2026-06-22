#include "RedBlackTree.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <cassert>

void verifyTree(const RedBlackTree<int, std::string>& rbt) {
    std::string errorMsg;
    bool success = rbt.verifyRBTProperties(errorMsg);
    std::cout << "[VERIFICATION] " << (success ? "\033[1;32mSUCCESS\033[0m" : "\033[1;31mFAILED\033[0m") 
              << ": " << errorMsg << std::endl;
    if (!success) {
        throw std::runtime_error("RBT Property Violation: " + errorMsg);
    }
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "          RED-BLACK TREE TEST SUITE" << std::endl;
    std::cout << "==================================================" << std::endl;

    RedBlackTree<int, std::string> rbt;

    // Test 1: Insertions that trigger different balance cases
    std::vector<std::pair<int, std::string>> insertions = {
        {15, "Fifteen"},
        {10, "Ten"},
        {20, "Twenty"},
        {5, "Five"},
        {12, "Twelve"},
        {18, "Eighteen"},
        {25, "Twenty-five"},
        {3, "Three"},
        {8, "Eight"},
        {11, "Eleven"},
        {14, "Fourteen"},
        {16, "Sixteen"},
        {19, "Nineteen"},
        {22, "Twenty-two"},
        {30, "Thirty"}
    };

    std::cout << "\n--- Inserting Elements step-by-step ---" << std::endl;
    for (const auto& pair : insertions) {
        std::cout << "Inserting Key: " << pair.first << " (" << pair.second << ")" << std::endl;
        rbt.insert(pair.first, pair.second);
    }

    std::cout << "\n--- Final Tree Structure ---" << std::endl;
    rbt.printTree();

    std::cout << "\n--- In-Order Traversal (Sorted order) ---" << std::endl;
    rbt.printInOrder();

    verifyTree(rbt);

    // Test 2: Search Operations
    std::cout << "\n--- Search Operations ---" << std::endl;
    std::vector<int> searchKeys = {12, 15, 30, 99};
    for (int key : searchKeys) {
        std::string* val = rbt.search(key);
        if (val) {
            std::cout << "Key " << key << " found! Value: " << *val << std::endl;
        } else {
            std::cout << "Key " << key << " NOT found!" << std::endl;
        }
    }

    // Test 3: Deletions and Balancing Fixups
    std::vector<int> deletionKeys = {
        3,  // Leaf deletion (simple case)
        12, // Node with one child
        20, // Node with two children (successor is 22)
        15  // Root deletion (complex cases)
    };

    std::cout << "\n--- Deleting Elements step-by-step ---" << std::endl;
    for (int key : deletionKeys) {
        std::cout << "\nDeleting Key: " << key << std::endl;
        bool deleted = rbt.remove(key);
        if (deleted) {
            std::cout << "Key " << key << " deleted successfully." << std::endl;
            rbt.printTree();
            verifyTree(rbt);
        } else {
            std::cout << "Key " << key << " not found, deletion skipped." << std::endl;
        }
    }

    // Test 4: Final checks
    std::cout << "\n--- Inserting values back to verify regrowth ---" << std::endl;
    rbt.insert(15, "Fifteen (Re-inserted)");
    rbt.insert(3, "Three (Re-inserted)");
    rbt.printTree();
    verifyTree(rbt);

    std::cout << "\n==================================================" << std::endl;
    std::cout << "    ALL RED-BLACK TREE TESTS COMPLETED SUCCESSFULLY!" << std::endl;
    std::cout << "==================================================" << std::endl;

    return 0;
}
