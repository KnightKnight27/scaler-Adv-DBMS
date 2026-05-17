#include "../include/ClockSweep.hpp"

int main() {

    ClockSweep<int> clockSweep(4);

    clockSweep.putKey(1);
    clockSweep.putKey(2);
    clockSweep.putKey(3);
    clockSweep.putKey(4);

    clockSweep.printCache();

    clockSweep.getKey(1);
    clockSweep.getKey(2);

    std::cout
        << "\nInserted 5 (causes eviction)\n";

    clockSweep.putKey(5);

    clockSweep.printCache();

    std::cout
        << "\nInserted 6 (causes eviction)\n";

    clockSweep.putKey(6);

    clockSweep.printCache();

    return 0;
}