#include "BPlusTree.h"
#include <iostream>

int main() {
    BPlusTree tree;

    // 1. Insert keys 1-20
    for (int i = 1; i <= 20; ++i)
        tree.insert(i, "val_" + std::to_string(i));

    std::cout << "=== Tree after inserting 1-20 ===\n";
    tree.print_tree();

    // 2. Search
    auto r1 = tree.search(7);
    std::cout << "\nSearch 7  -> " << (r1 ? "Found: " + *r1 : "Not found") << "\n";

    auto r2 = tree.search(25);
    std::cout << "Search 25 -> " << (r2 ? "Found: " + *r2 : "Not found: 25") << "\n";

    // 3. Range scan [5, 12]
    std::cout << "\nRange scan [5, 12]:\n";
    for (auto& [k, v] : tree.range_scan(5, 12))
        std::cout << "  " << k << " -> " << v << "\n";

    // 4. Delete keys 3, 7, 15
    tree.delete_key(3);
    tree.delete_key(7);
    tree.delete_key(15);

    std::cout << "\n=== Tree after deleting 3, 7, 15 ===\n";
    tree.print_tree();

    // 5. Range scan [5, 12] again — key 7 should be gone
    std::cout << "\nRange scan [5, 12] after deletions:\n";
    for (auto& [k, v] : tree.range_scan(5, 12))
        std::cout << "  " << k << " -> " << v << "\n";

    return 0;
}
