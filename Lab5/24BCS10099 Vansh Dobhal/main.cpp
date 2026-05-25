#include "btree.hpp"
#include "rbt.hpp"

#include <iostream>

void printSearchResult(const char* treeName, int key, bool found) {
    std::cout << "Search " << key << " in " << treeName << ": "
              << (found ? "FOUND" : "NOT FOUND") << '\n';
}

int main() {
    std::cout << "========================================\n";
    std::cout << " Lab 5 - B-Tree and Red-Black Tree\n";
    std::cout << " Vansh Dobhal, 24BCS10099\n";
    std::cout << "========================================\n";

    BTree btree;
    int btreeKeys[] = {42, 12, 7, 29, 18, 55, 3, 9, 33, 47, 61, 1, 24, 38, 50};

    std::cout << "\nInserting into B-Tree:\n";
    for (int key : btreeKeys) {
        btree.insert(key);
        std::cout << key << ' ';
    }
    std::cout << '\n';

    btree.printStructure();
    btree.traverse();
    printSearchResult("B-Tree", 33, btree.search(33) != nullptr);
    printSearchResult("B-Tree", 99, btree.search(99) != nullptr);

    RedBlackTree rbt;
    int rbtKeys[] = {36, 18, 52, 9, 27, 45, 60, 4, 14, 22, 31, 40, 48, 57, 63};

    std::cout << "\nInserting into Red-Black Tree:\n";
    for (int key : rbtKeys) {
        rbt.insert(key);
        std::cout << key << ' ';
    }
    std::cout << '\n';

    rbt.printStructure();
    rbt.inorder();
    printSearchResult("Red-Black Tree", 22, rbt.search(22) != nullptr);
    printSearchResult("Red-Black Tree", 100, rbt.search(100) != nullptr);

    std::cout << "\nDone.\n";
    return 0;
}