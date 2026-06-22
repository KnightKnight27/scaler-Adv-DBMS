#include <iostream>
#include "BTree.h"
#include "RedBlackTree.h"

int main() {
    std::cout << "Lab 5 — B-Tree and Red-Black Tree demo\n";

    BTree bt(2);
    int keys[] = {10, 20, 5, 6, 12, 30, 7, 17};
    for (int k : keys) bt.insert(k);
    std::cout << "B-Tree traversal: ";
    bt.traverse(std::cout);
    std::cout << "\n";

    RBTree rbt;
    int rkeys[] = {7, 3, 18, 10, 22, 8, 11, 26};
    for (int k : rkeys) rbt.insert(k);
    std::cout << "Red-Black Tree inorder: ";
    rbt.inorderPrint(std::cout);
    std::cout << "\n";

    return 0;
}
