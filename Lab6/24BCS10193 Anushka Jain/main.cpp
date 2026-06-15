#include "b_tree.hpp"
#include <iostream>

using namespace std;

// ADBMS LAB 6
// Roll No: 24BCS10193
// Name: Anushka Jain

int main() {
    BTree tree(3);

    int values[] = {15, 8, 22, 5, 12, 18, 30, 2, 10, 14};

    for(int val : values)
        tree.insert(val);

    cout << "B-Tree Traversal:\n";
    tree.traverse();

    cout << "\n\n";

    int searchKey = 18;

    if(tree.search(searchKey))
        cout << searchKey << " found in tree.\n";
    else
        cout << searchKey << " not found.\n";

    return 0;
}