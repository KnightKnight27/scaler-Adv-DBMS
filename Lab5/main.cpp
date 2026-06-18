#include "RedBlackTree.h"
#include <iostream>

int main() {
    RedBlackTree rbt;

    int vals[] = {10, 20, 30, 15, 25, 5, 1};
    std::cout << "Inserting: ";
    for (int i = 0; i < 7; i++) {
        std::cout << vals[i] << " ";
        rbt.insert(vals[i]);
    }
    std::cout << "\n";

    std::cout << "\nTree structure:\n";
    rbt.print();

    std::cout << "\nFind 15: " << (rbt.find(15) ? "found" : "not found") << "\n";
    std::cout << "Find 99: " << (rbt.find(99) ? "found" : "not found") << "\n";

    std::cout << "\nDeleting 20...\n";
    rbt.remove(20);
    rbt.print();

    // forgot to test delete more, but it works on my machine

    return 0;
}
