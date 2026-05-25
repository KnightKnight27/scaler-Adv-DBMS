#include "RedBlackTree.h"
#include <iostream>

int main() {
    RedBlackTree tree;

    std::cout << "=== Red-Black Tree Demo ===\n\n";

    std::cout << "── Inserting values ──\n\n";

    int values[] = {10, 20, 30, 15, 25, 5, 1, 7, 12, 18};
    int n = static_cast<int>(sizeof(values) / sizeof(values[0]));

    for (int i = 0; i < n; i++) {
        std::cout << "Insert " << values[i] << "\n";
        tree.insert(values[i]);
        tree.printInorder();
        std::cout << "Valid : " << (tree.isValid() ? "YES" : "NO") << "\n\n";
    }

    std::cout << "── Final tree (level order) ──\n";
    tree.printLevels();
    std::cout << "\n";

    std::cout << "── Search tests ──\n";
    for (int v : {10, 7, 99, 25, 1}) {
        std::cout << "  Search " << v << " : "
                  << (tree.search(v) ? "FOUND" : "NOT FOUND") << "\n";
    }
    std::cout << "\n";

    std::cout << "── Removing values: 10, 20, 5 ──\n\n";

    for (int v : {10, 20, 5}) {
        std::cout << "Remove " << v << "\n";
        tree.remove(v);
        tree.printInorder();
        std::cout << "Valid : " << (tree.isValid() ? "YES" : "NO") << "\n\n";
    }

    std::cout << "── Tree after removals (level order) ──\n";
    tree.printLevels();

    return 0;
}
