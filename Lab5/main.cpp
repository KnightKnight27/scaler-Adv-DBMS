#include "RedBlackTree.h"
#include <iostream>

int main() {
    RedBlackTree tree;

    for (int val : {10, 20, 30, 15, 25, 5, 1}) {
        tree.insert(val);
        std::cout << "insert " << val << " -> ";
        tree.print();
    }

    std::cout << "\nsearch 15: " << (tree.search(15) ? "found" : "not found") << "\n";
    std::cout << "search 99: " << (tree.search(99) ? "found" : "not found") << "\n";

    return 0;
}
