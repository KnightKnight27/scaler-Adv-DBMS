#include "b_tree.hpp"
#include <iostream>

int main() {
    // ADBMS LAB 6
    // Roll No: 24BCS10199
    // Name: Ayushkumar Singh

    cout << "===== B-Tree Implementation Demo =====" << endl;

    // Create a B-Tree with minimum degree 3
    BTree t(3);

    t.insert(10);
    t.insert(20);
    t.insert(5);
    t.insert(6);
    t.insert(12);
    t.insert(30);
    t.insert(7);
    t.insert(17);

    cout << "Traversal of the constructed B-Tree is: ";
    t.traverse();
    cout << endl;

    int key1 = 6;
    cout << "Searching for key " << key1 << ": ";
    if (t.search(key1) != nullptr) {
        cout << "Found" << endl;
    } else {
        cout << "Not Found" << endl;
    }

    int key2 = 15;
    cout << "Searching for key " << key2 << ": ";
    if (t.search(key2) != nullptr) {
        cout << "Found" << endl;
    } else {
        cout << "Not Found" << endl;
    }

    return 0;
}
