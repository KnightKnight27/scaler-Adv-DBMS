#include "ClockSweep.h"
#include <vector>
#include <iostream>

// Simulate page replacement with clock sweep algorithm
int main() {
    std::cout << "=== Clock Sweep Page Replacement Algorithm Demo ===\n\n";

    constexpr int buffer_capacity = 4;
    ClockSweep<int> page_buffer{buffer_capacity};

    const std::vector<int> page_access_sequence = {1, 2, 3, 4, 1, 5, 2, 1};

    // Process each page access
    for (const int page_number : page_access_sequence) {
        // If page miss, load the page into buffer
        if (!page_buffer.getKey(page_number)) {
            page_buffer.putKey(page_number);
        }
    }

    // Display final buffer state and statistics
    page_buffer.printBuffer();
    page_buffer.printStats();

    return 0;
}
