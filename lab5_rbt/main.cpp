#include <iostream>
#include "RedBlackTree.h"

using namespace std;

int main() {
    RedBlackTree tree;

    cout << "Inserting elements into the Red-Black Tree..." << endl;
    int elements[] = {10, 20, 30, 15, 25, 5, 1};
    for (int el : elements) {
        tree.insert(el);
    }

    cout << "In-order Traversal (Value and Color):" << endl;
    tree.printInOrder();

    cout << "\nSearching for elements:" << endl;
    cout << "Search 15: " << (tree.search(15) ? "Found" : "Not Found") << endl;
    cout << "Search 100: " << (tree.search(100) ? "Found" : "Not Found") << endl;

    return 0;
}
