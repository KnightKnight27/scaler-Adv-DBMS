#include <iostream>
#include "../include/ClockSweep.hpp"

int main() {
    ClockSweep<int> cs(4);

    cs.putKey(1);
    cs.putKey(2);
    cs.putKey(3);
    cs.putKey(4);
    cs.printCache();

    // access 1 and 2 so they get a second chance
    cs.getKey(1);
    cs.getKey(2);

    std::cout << "\nInserting 5 (eviction expected):\n";
    cs.putKey(5);
    cs.printCache();

    std::cout << "\nInserting 6 (eviction expected):\n";
    cs.putKey(6);
    cs.printCache();

    return 0;
}
