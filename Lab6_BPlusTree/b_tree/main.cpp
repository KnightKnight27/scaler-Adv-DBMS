#include <iostream>
#include "BTree.h"

int main() {

    BTree tree(3);

    std::cout << "=== Inserting Keys ===\n";

    tree.insert(10);
    tree.insert(20);
    tree.insert(5);
    tree.insert(6);
    tree.insert(12);
    tree.insert(30);
    tree.insert(7);
    tree.insert(17);

    std::cout << "B-Tree Traversal:\n";
    tree.traverse();

    std::cout << "\n";

    int searchKey = 12;

    if (tree.search(searchKey))
        std::cout << searchKey << " found in B-Tree\n";
    else
        std::cout << searchKey << " not found in B-Tree\n";

    std::cout << "\n=== Deleting Keys ===\n";

    std::cout << "Deleting 6\n";
    tree.remove(6);
    tree.traverse();

    std::cout << "Deleting 7\n";
    tree.remove(7);
    tree.traverse();

    std::cout << "Deleting 12\n";
    tree.remove(12);
    tree.traverse();

    std::cout << "\nFinal B-Tree Traversal:\n";
    tree.traverse();

    return 0;
}
