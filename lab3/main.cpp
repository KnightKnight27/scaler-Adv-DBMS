/**
 * Lab 3 — ClockSweep Buffer Pool: Driver Program
 *
 * Demonstrates PostgreSQL's buffer pool replacement algorithm with
 * multiple test scenarios showing eviction behavior, hit rates,
 * and comparison with naive strategies.
 */

#include "clock_sweep.h"
#include <iostream>
#include <vector>
#include <random>
#include <chrono>

// ─────────────────────────────────────────────────
// Test 1: Basic Buffer Pool Operations
// ─────────────────────────────────────────────────
void test_basic_operations() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Test 1: Basic Buffer Pool Operations                       ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    DiskManager disk;
    ClockSweepBufferPool pool(4, disk);  // 4-frame buffer pool

    std::cout << "\n--- Initial state (empty pool) ---" << std::endl;
    pool.print_state();

    // Fetch 4 pages (fills the pool)
    std::cout << "\n--- Fetching pages 10, 20, 30, 40 ---" << std::endl;
    for (uint32_t pid : {10u, 20u, 30u, 40u}) {
        pool.fetch_page(pid);
        pool.unpin_page(pid);  // unpin immediately
    }
    pool.print_state();

    // Access page 20 again (should be a hit)
    std::cout << "\n--- Re-accessing page 20 (cache hit) ---" << std::endl;
    pool.fetch_page(20);
    pool.unpin_page(20);
    pool.print_state();
    std::cout << "  Page 20's usage_count increased → protected from eviction!" << std::endl;

    // Fetch a new page (triggers eviction via ClockSweep)
    std::cout << "\n--- Fetching page 50 (triggers ClockSweep eviction) ---" << std::endl;
    pool.fetch_page(50);
    pool.unpin_page(50);
    pool.print_state();
    std::cout << "  ClockSweep scanned the buffer, decrementing usage counts." << std::endl;
    std::cout << "  The frame with usage_count=0 was evicted." << std::endl;

    pool.print_stats();
}

// ─────────────────────────────────────────────────
// Test 2: Dirty Page Handling
// ─────────────────────────────────────────────────
void test_dirty_pages() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Test 2: Dirty Page Handling                                ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    DiskManager disk;
    ClockSweepBufferPool pool(3, disk);

    // Fetch and modify pages
    std::cout << "\n--- Fetching and modifying pages ---" << std::endl;
    pool.fetch_page(1);
    pool.unpin_page(1, true);  // mark as dirty (modified)

    pool.fetch_page(2);
    pool.unpin_page(2, false);  // clean (read-only)

    pool.fetch_page(3);
    pool.unpin_page(3, true);  // dirty

    pool.print_state();

    // Fetch page 4 — eviction must flush the dirty victim
    std::cout << "\n--- Fetching page 4 (evicts dirty page → forced disk write) ---" << std::endl;
    pool.fetch_page(4);
    pool.unpin_page(4);
    pool.print_state();
    pool.print_stats();

    std::cout << "\n  Note: Evicting a dirty page requires an extra disk write (flush)." << std::endl;
    std::cout << "  PostgreSQL's bgwriter flushes dirty pages in the background" << std::endl;
    std::cout << "  to reduce the chance of eviction-time writes." << std::endl;
}

// ─────────────────────────────────────────────────
// Test 3: Pin Count Protection
// ─────────────────────────────────────────────────
void test_pin_protection() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Test 3: Pin Count Protection                               ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    DiskManager disk;
    ClockSweepBufferPool pool(3, disk);

    // Pin all pages (simulate concurrent backends holding pages)
    std::cout << "\n--- Pinning all 3 frames ---" << std::endl;
    pool.fetch_page(100);  // pin_count = 1
    pool.fetch_page(200);  // pin_count = 1
    pool.fetch_page(300);  // pin_count = 1
    pool.print_state();

    // Try to fetch a 4th page — should fail (all pinned)
    std::cout << "\n--- Attempting to fetch page 400 (all frames pinned!) ---" << std::endl;
    try {
        pool.fetch_page(400);
        std::cout << "  ERROR: Should have thrown!" << std::endl;
    } catch (const std::runtime_error& e) {
        std::cout << "  Exception caught: " << e.what() << std::endl;
        std::cout << "  This is expected! All frames are pinned, no victim available." << std::endl;
    }

    // Unpin one page and try again
    std::cout << "\n--- Unpinning page 200, then retrying fetch ---" << std::endl;
    pool.unpin_page(200);
    pool.fetch_page(400);
    pool.unpin_page(400);
    pool.print_state();
    std::cout << "  Page 200 was evicted since it was the only unpinned frame." << std::endl;

    // Cleanup: unpin remaining
    pool.unpin_page(100);
    pool.unpin_page(300);
}

// ─────────────────────────────────────────────────
// Test 4: Usage Count and Second Chances
// ─────────────────────────────────────────────────
void test_usage_count_sweep() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Test 4: Usage Count & Second Chances                       ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    DiskManager disk;
    ClockSweepBufferPool pool(4, disk);

    // Fill the pool
    for (uint32_t pid : {1u, 2u, 3u, 4u}) {
        pool.fetch_page(pid);
        pool.unpin_page(pid);
    }

    // Access some pages multiple times to increase usage_count
    std::cout << "\n--- Accessing page 2 three extra times (hot page) ---" << std::endl;
    for (int i = 0; i < 3; ++i) {
        pool.fetch_page(2);
        pool.unpin_page(2);
    }

    std::cout << "--- Accessing page 4 one extra time ---" << std::endl;
    pool.fetch_page(4);
    pool.unpin_page(4);

    pool.print_state();
    std::cout << "  Page 2 has high usage_count → will get many 'second chances'" << std::endl;
    std::cout << "  Page 1 and 3 have low usage_count → will be evicted first" << std::endl;

    // Now fetch new pages and observe eviction order
    std::cout << "\n--- Fetching pages 10, 11, 12 (observe eviction order) ---" << std::endl;
    for (uint32_t pid : {10u, 11u, 12u}) {
        std::cout << "\n  Fetching page " << pid << ":" << std::endl;
        pool.fetch_page(pid);
        pool.unpin_page(pid);
        pool.print_state();
    }

    std::cout << "\n  Observation: Pages with higher usage_count survived longer." << std::endl;
    std::cout << "  This approximates LRU behavior — frequently accessed pages" << std::endl;
    std::cout << "  are protected, while cold pages are evicted." << std::endl;
}

// ─────────────────────────────────────────────────
// Test 5: Workload Simulation (Sequential vs Random)
// ─────────────────────────────────────────────────
void test_workload_simulation() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Test 5: Workload Simulation                                ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    const uint32_t POOL_SIZE = 8;
    const uint32_t NUM_PAGES = 50;
    const uint32_t NUM_ACCESSES = 500;

    // Scenario A: Sequential scan (worst case for buffer pool)
    {
        std::cout << "\n--- Scenario A: Sequential Scan ---" << std::endl;
        std::cout << "  Pool size: " << POOL_SIZE << ", Scanning " << NUM_PAGES
                  << " pages sequentially, " << NUM_ACCESSES << " accesses" << std::endl;

        DiskManager disk;
        ClockSweepBufferPool pool(POOL_SIZE, disk);

        for (uint32_t i = 0; i < NUM_ACCESSES; ++i) {
            uint32_t page_id = i % NUM_PAGES;
            pool.fetch_page(page_id);
            pool.unpin_page(page_id);
        }

        pool.print_stats();
        std::cout << "  Sequential scan has poor hit rate when #pages > pool_size" << std::endl;
    }

    // Scenario B: Zipfian distribution (realistic: some pages are hot)
    {
        std::cout << "\n--- Scenario B: Zipfian (Skewed) Workload ---" << std::endl;
        std::cout << "  Pool size: " << POOL_SIZE << ", " << NUM_PAGES
                  << " total pages, " << NUM_ACCESSES << " accesses" << std::endl;
        std::cout << "  80% of accesses go to 20% of pages (hot set)" << std::endl;

        DiskManager disk;
        ClockSweepBufferPool pool(POOL_SIZE, disk);

        std::mt19937 rng(42);
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        uint32_t hot_pages = NUM_PAGES / 5;  // 20% hot

        for (uint32_t i = 0; i < NUM_ACCESSES; ++i) {
            uint32_t page_id;
            if (dist(rng) < 0.8) {
                // 80% chance: access a hot page
                page_id = rng() % hot_pages;
            } else {
                // 20% chance: access a cold page
                page_id = hot_pages + (rng() % (NUM_PAGES - hot_pages));
            }
            pool.fetch_page(page_id);
            pool.unpin_page(page_id);
        }

        pool.print_stats();
        std::cout << "  Skewed workload has much better hit rate!" << std::endl;
        std::cout << "  Hot pages accumulate high usage_count → stay in the pool" << std::endl;
    }

    // Scenario C: Working set that fits in the pool
    {
        std::cout << "\n--- Scenario C: Working Set Fits in Pool ---" << std::endl;
        std::cout << "  Pool size: " << POOL_SIZE << ", Working set: "
                  << POOL_SIZE << " pages, " << NUM_ACCESSES << " accesses" << std::endl;

        DiskManager disk;
        ClockSweepBufferPool pool(POOL_SIZE, disk);

        std::mt19937 rng(42);

        for (uint32_t i = 0; i < NUM_ACCESSES; ++i) {
            uint32_t page_id = rng() % POOL_SIZE;  // only access pages 0..7
            pool.fetch_page(page_id);
            pool.unpin_page(page_id);
        }

        pool.print_stats();
        std::cout << "  Perfect scenario: working set fits → near-100% hit rate" << std::endl;
    }
}

// ─────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────
int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Lab 3: ClockSweep Buffer Pool Replacement Algorithm       ║" << std::endl;
    std::cout << "║  (PostgreSQL's Eviction Strategy)                          ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    test_basic_operations();
    test_dirty_pages();
    test_pin_protection();
    test_usage_count_sweep();
    test_workload_simulation();

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Lab 3 Complete!                                            ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    return 0;
}
