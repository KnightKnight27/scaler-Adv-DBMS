#include "RedBlackTree.h"
#include <iostream>

int main() {
    RedBlackTree tree;

    // insert in order that triggers all fix-up cases
    for (int val : {10, 20, 30, 15, 25, 5, 1}) {
        tree.insert(val);
        std::cout << "insert " << val << " -> ";
        tree.print();
    }

    std::cout << "\nfind 15: " << (tree.find(15) ? "found" : "not found") << "\n";
    std::cout << "find 99: " << (tree.find(99) ? "found" : "not found") << "\n";

    return 0;
}
