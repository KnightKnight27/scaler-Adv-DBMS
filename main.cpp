#include <iostream>
#include "RBTree.hpp"
#include "BTree.hpp"

int main() {
    
    // Red-Black Tree Verification
    
    std::cout << "        Red-Black Tree Tests            \n";
    
    RBTree rbTree;
    int rbKeys[] = {20, 15, 25, 10, 5, 1, 30};
    
    for (int k : rbKeys) {
        std::cout << "Inserting " << k << " into RB-Tree...\n";
        rbTree.insert(k);
    }
    
    std::cout << "\nRB-Tree Pre-order Traversal (Value(Color)): ";
    rbTree.printTree();

    std::cout << "\nRemoving key 15 from RB-Tree...\n";
    rbTree.deleteNode(15);
    
    std::cout << "RB-Tree Pre-order Traversal after deletion: ";
    rbTree.printTree();


    
    // B-Tree Verification (Degree t = 3)
    
    std::cout << "       B-Tree Tests (Degree t=3)        \n";
    
    BTree bTree(3);
    int bKeys[] = {1, 3, 7, 10, 11, 13, 14, 15, 18, 16, 19, 24, 25, 26, 21, 4, 5, 20, 22, 2, 17, 12, 6};
    
    for (int k : bKeys) {
        bTree.insert(k);
    }
    
    std::cout << "B-Tree Traversal after all insertions:\n";
    bTree.traverse();

    std::cout << "\nRemoving key 6 (Leaf Node structural adjustment):\n";
    bTree.remove(6);
    bTree.traverse();

    std::cout << "\nRemoving key 13 (Internal Node split/merge/borrow verification):\n";
    bTree.remove(13);
    bTree.traverse();

    return 0;
}