#include <iostream>
#include "RBTree.hpp"
#include "BTree.hpp"

int main() {
    std::cout << "=== Red-Black Tree Tests ===\n";
    RBTree rbTree;
    
    int rbKeys[] = {20, 15, 25, 10, 5, 1, 30};
    for(int k : rbKeys) {
        std::cout << "Inserting " << k << " into RB-Tree.\n";
        rbTree.insert(k);
    }
    
    std::cout << "RB-Tree Pre-order (Value(Color)): ";
    rbTree.printTree();

    std::cout << "\nDeleting 15 from RB-Tree.\n";
    rbTree.deleteNode(15);
    std::cout << "RB-Tree Pre-order after deletion: ";
    rbTree.printTree();


    std::cout << "\n\n=== B-Tree Tests (Degree t=3) ===\n";
    BTree bTree(3);

    int bKeys[] = {1, 3, 7, 10, 11, 13, 14, 15, 18, 16, 19, 24, 25, 26, 21, 4, 5, 20, 22, 2, 17, 12, 6};
    for(int k : bKeys) {
        bTree.insert(k);
    }
    
    std::cout << "B-Tree traversal after insertions: ";
    bTree.traverse();

    std::cout << "\nDeleting 6 (Leaf node)\n";
    bTree.remove(6);
    bTree.traverse();

    std::cout << "Deleting 13 (Internal node, triggers complex merge/borrow)\n";
    bTree.remove(13);
    bTree.traverse();

    return 0;
}