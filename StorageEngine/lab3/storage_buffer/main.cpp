#include "ClockSweep.hpp"
#include <string>

int main() {
    ClockSweep<int, std::string> pool(4);

    pool.put(1, "page-A");
    pool.put(2, "page-B");
    pool.put(3, "page-C");
    pool.put(4, "page-D");
    std::cout << "After filling:     ";
    pool.dump();

    pool.get(1);
    pool.get(1);
    pool.get(1);
    pool.get(2);
    std::cout << "After hot reads:   ";
    pool.dump();

    pool.put(5, "page-E");
    std::cout << "After put(5):      ";
    pool.dump();

    auto v5 = pool.get(5);
    if (v5) std::cout << "get(5) -> " << *v5 << "\n";

    auto v3 = pool.get(3);
    if (!v3) std::cout << "get(3) -> MISS (evicted)\n";

    return 0;
}
