#include <iostream>
#include "BTree.h"

int main() {
    BTree<int> tree(3);
    int values[] = {10, 20, 5, 6, 12, 30, 7, 17};

    std::cout << "Inserting values into B-tree:\n";
    for (int value : values) {
        std::cout << value << " ";
        tree.insert(value);
    }
    std::cout << "\n\n";

    std::cout << "B-tree traversal (ascending):\n";
    tree.traverse();
    std::cout << "\n";

    int searchKeys[] = {6, 15, 17, 100};
    for (int key : searchKeys) {
        std::cout << "Search " << key << ": "
                  << (tree.search(key) ? "found" : "not found") << "\n";
    }

    return 0;
}
