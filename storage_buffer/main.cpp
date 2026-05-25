#include "ClockSweep.h"

#include <iostream>
#include <string>

int main() {
    ClockSweep<int> integer_cache(3);
    integer_cache.put(10);
    integer_cache.put(20);
    integer_cache.put(30);

    if (const auto page = integer_cache.get(20); page.has_value()) {
        std::cout << "Found integer page: " << *page << '\n';
    }

    ClockSweep<std::string> string_cache(2);
    string_cache.put("users_page");
    string_cache.put("orders_page");

    if (const auto page = string_cache.get("users_page"); page.has_value()) {
        std::cout << "Found string page: " << *page << '\n';
    }

    return 0;
}
