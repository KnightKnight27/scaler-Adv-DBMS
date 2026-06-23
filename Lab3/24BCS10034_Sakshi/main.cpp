#include <iostream>
#include "clock_sweep.h"

/*
 * main.cpp — demonstrates Clock Sweep buffer pool
 *
 * Scenario mirrors PostgreSQL buffer manager usage:
 *   1. Fill the pool to capacity
 *   2. Access some pages (bump their usage counts)
 *   3. Insert new pages — clock sweep finds victims among low-usage frames
 */

int main() {

    // ── 1. Create buffer pool with 5 frames (like your teacher's boilerplate) ──
    ClockSweep<int> clockSweep(5);

    std::cout << "\n=== PHASE 1: Fill the buffer pool ===\n";
    clockSweep.putKey(10);
    clockSweep.putKey(20);
    clockSweep.putKey(30);
    clockSweep.putKey(40);
    clockSweep.putKey(50);
    clockSweep.printState();

    // ── 2. Access some keys — bumps usage counts ──────────────────────────────
    std::cout << "=== PHASE 2: Access pages (pin them) ===\n";
    clockSweep.getKey(10);  // usage: 10→2, 20→1, 30→1, 40→1, 50→1
    clockSweep.getKey(10);  // usage: 10→3
    clockSweep.getKey(30);  // usage: 30→2
    clockSweep.printState();

    // ── 3. Insert new key — pool is full, clock sweep must evict ─────────────
    // Expected: clock hand sweeps from 0, decrements usage counts.
    // First frame with usage==0 gets evicted.
    // keys 20, 40, 50 (usage=1) are candidates; 10 and 30 have usage>1.
    std::cout << "=== PHASE 3: Insert 60 (pool full — triggers eviction) ===\n";
    clockSweep.putKey(60);
    clockSweep.printState();

    std::cout << "=== PHASE 4: Insert 70 (another eviction) ===\n";
    clockSweep.putKey(70);
    clockSweep.printState();

    // ── 4. Test cache miss ────────────────────────────────────────────────────
    std::cout << "=== PHASE 5: Cache miss test ===\n";
    try {
        clockSweep.getKey(20);   // 20 was likely evicted
    } catch (const std::runtime_error& e) {
        std::cout << e.what() << " (expected — 20 was evicted)\n";
    }

    // ── 5. Template works with strings too ───────────────────────────────────
    std::cout << "\n=== PHASE 6: String key buffer pool ===\n";
    ClockSweep<std::string> strPool(3);
    strPool.putKey("users_page_1");
    strPool.putKey("orders_page_1");
    strPool.putKey("products_page_1");
    strPool.getKey("users_page_1");
    strPool.putKey("accounts_page_1");   // should evict orders_page_1
    strPool.printState();

    std::cout << "=== Done ===\n";
    return 0;
}