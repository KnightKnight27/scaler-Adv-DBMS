#include "ClockSweep.h"

int main() {
    std::cout << "=== Clock Sweep Demo ===\n\n";

    ClockSweep<int> cache(4);

    // simulating page accesses
    std::vector<int> pages = {1, 2, 3, 4, 1, 5, 2, 1};

    for (int page : pages) {
        if (!cache.getKey(page)) {
            cache.putKey(page);
        }
    }

    cache.printBuffer();
    cache.printStats();

    return 0;
}
