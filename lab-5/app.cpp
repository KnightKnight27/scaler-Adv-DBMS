#include "RBTree.h"

#include <iostream>
#include <vector>

int main() {

    RBTree rb;

    std::vector<int> values = {
        50,
        40,
        60,
        30,
        45,
        55,
        70
    };

    for (int x : values) {

        rb.insertValue(x);

        std::cout
            << "After inserting "
            << x
            << " : ";

        rb.display();
    }

    std::cout << "\n";

    int targets[] = {45, 100};

    for (int t : targets) {

        std::cout
            << "Searching "
            << t
            << " -> ";

        if (rb.contains(t))
            std::cout << "Exists\n";
        else
            std::cout << "Does Not Exist\n";
    }

    return 0;
}