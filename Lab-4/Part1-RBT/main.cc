// Lab 4 (Part 1) - Red-Black Tree demo driver.
//
// Builds a tree from a fixed key list, runs find / remove / print, and
// validates the five RB invariants after every mutation via
// checkInvariants().

#include "RedBlackTree.h"

#include <iostream>
#include <vector>

int main() {
    RedBlackTree t;

    const std::vector<int> keys = {41, 38, 31, 12, 19, 8, 6, 25,
                                   17, 53, 47, 22, 77, 1, 63};

    std::cout << "== inserting " << keys.size() << " keys ==\n";
    for (int k : keys) {
        t.insert(k);
        t.checkInvariants();
    }

    std::cout << "\n== tree after inserts ==\n";
    t.print();
    std::cout << "black-height = " << t.checkInvariants() << "\n";

    std::cout << "\n== find ==\n";
    for (int k : {38, 100, 19, 6}) {
        std::cout << "find(" << k << ") = " << (t.find(k) ? "true" : "false") << "\n";
    }

    std::cout << "\n== remove ==\n";
    for (int k : {19, 41, 1, 77, 25}) {
        bool ok = t.remove(k);
        std::cout << "remove(" << k << ") -> " << (ok ? "ok" : "miss") << "\n";
        t.checkInvariants();
    }

    std::cout << "\n== tree after deletes ==\n";
    t.print();
    std::cout << "black-height = " << t.checkInvariants() << "\n";

    return 0;
}
