#include "RedBlackTree.h"
#include <iostream>

int main() {
    RedBlackTree rbt;

    std::cout << "Inserting: 10 20 30 15 25 5 1\n";
    for (int v : {10, 20, 30, 15, 25, 5, 1})
        rbt.insert(v);

    std::cout << "\nTree structure:\n";
    rbt.print();

    std::cout << "\nFind 15: " << (rbt.find(15) ? "found" : "not found") << "\n";
    std::cout << "Find 99: " << (rbt.find(99) ? "found" : "not found") << "\n";

    std::cout << "\nDeleting 20...\n";
    rbt.remove(20);
    rbt.print();

    std::cout << "\nDeleting 10...\n";
    rbt.remove(10);
    rbt.print();

    return 0;
}
