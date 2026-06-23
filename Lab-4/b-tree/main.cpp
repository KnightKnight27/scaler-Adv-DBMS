#include "BTree.hpp"

int main() {
    BTree tree(3);

    const int insert_values[] = {10, 20, 5, 6, 12, 30, 7, 17, 3, 2, 15, 16, 25, 40, 50, 60};
    for (int value : insert_values) {
        tree.insert(value);
    }

    std::cout << "B-Tree after inserts: ";
    tree.printInOrder(std::cout);

    const int delete_values[] = {6, 13, 7, 4, 2, 16, 17, 10, 20};
    for (int value : delete_values) {
        tree.remove(value);
    }

    std::cout << "B-Tree after deletes: ";
    tree.printInOrder(std::cout);
    std::cout << "Contains 15? " << (tree.contains(15) ? "yes" : "no") << '\n';
    return 0;
}