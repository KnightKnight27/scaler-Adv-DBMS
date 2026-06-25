#include "red-black-tree.h"
#include <iostream>

int main() {
    RedBlackTree tree;

    const int insert_values[] = {41, 38, 31, 12, 19, 8, 50, 60, 55, 65};
    for (int v : insert_values) tree.insert(v);

    std::cout << "Red-Black Tree after inserts: ";
    tree.printInOrder(std::cout);

    std::cout << "Contains 31? " << (tree.contains(31) ? "yes" : "no") << '\n';

    const int delete_values[] = {8, 12, 19, 31};
    for (int v : delete_values) tree.remove(v);

    std::cout << "Red-Black Tree after deletes: ";
    tree.printInOrder(std::cout);

    return 0;
}
