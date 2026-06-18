#include "BTree.h"
#include <iostream>

int main() {
    BTree bt;

    int data[] = {10, 20, 5, 6, 12, 30, 7, 17};
    int n = sizeof(data) / sizeof(data[0]);

    std::cout << "inserting:";
    for (int i = 0; i < n; i++) {
        std::cout << " " << data[i];
        bt.insert(data[i]);
    }
    std::cout << "\n\n";

    std::cout << "tree (sideways, root on left):\n";
    bt.print();
    std::cout << "\n";

    std::cout << "find 6:  " << (bt.find(6)  ? "yes" : "no") << "\n";
    std::cout << "find 17: " << (bt.find(17) ? "yes" : "no") << "\n";
    std::cout << "find 99: " << (bt.find(99) ? "yes" : "no") << "\n";

    return 0;
}
