#include "btree.hpp"
#include "rbt.hpp"
#include <iostream>

using namespace std;

int main() {
    cout << "========================================\n";
    cout << "   Lab 5 - B-Tree & Red-Black Tree\n";
    cout << "========================================\n\n";

    // ─── B-Tree Demo ─────────────────────────────────────────────────────────
    cout << "--- B-Tree (Order " << T << ") ---\n";
    BTree btree;

    int btreeKeys[] = {10, 20, 5, 6, 12, 30, 7, 17, 3, 25, 40, 50, 1};
    for (int key : btreeKeys)
        btree.insert(key);

    btree.printTree();
    btree.traverse();

    // Search demo
    int searchKey = 17;
    BTreeNode* found = btree.search(searchKey);
    cout << "\nSearch " << searchKey << " in B-Tree: "
         << (found ? "FOUND" : "NOT FOUND") << "\n";

    searchKey = 99;
    found = btree.search(searchKey);
    cout << "Search " << searchKey << " in B-Tree: "
         << (found ? "FOUND" : "NOT FOUND") << "\n";

    cout << "\n";

    // ─── Red-Black Tree Demo ─────────────────────────────────────────────────
    cout << "--- Red-Black Tree ---\n";
    RedBlackTree rbt;

    int rbtKeys[] = {15, 25, 8, 3, 12, 20, 30, 1, 5, 10, 18, 28};
    for (int key : rbtKeys)
        rbt.insert(key);

    rbt.printTree();
    rbt.inorder();

    // Search demo
    int rbtSearch = 12;
    Node* rbtFound = rbt.search(rbtSearch);
    cout << "\nSearch " << rbtSearch << " in RBT: ";
    if (rbtFound && rbtFound->data == rbtSearch)
        cout << "FOUND  [color: " << (rbtFound->color == RED ? "RED" : "BLACK") << "]\n";
    else
        cout << "NOT FOUND\n";

    rbtSearch = 99;
    rbtFound = rbt.search(rbtSearch);
    cout << "Search " << rbtSearch << " in RBT: ";
    if (rbtFound && rbtFound->data == rbtSearch)
        cout << "FOUND\n";
    else
        cout << "NOT FOUND\n";

    cout << "\n========================================\n";
    cout << "   Done.\n";
    cout << "========================================\n";

    return 0;
}