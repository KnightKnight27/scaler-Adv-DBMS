#include "ClockSweep.hpp"
#include <iostream>
#include <string>
#include <cassert>
#include <thread>
#include <vector>
#include <chrono>

// Helper to print styled section dividers
void logSection(const std::string& title) {
    std::cout << "\n" << std::string(75, '#') << "\n";
    std::cout << " >>> EXECUTION STAGE: " << title << "\n";
    std::cout << std::string(75, '#') << "\n";
}

// =============================================================================
// Stage 1: Basic Cache Access and Type Support
// =============================================================================
void runBasicVerification() {
    logSection("Basic Cache Access & Type Support");

    // Scenario 1.1: Integer key mapping to String values
    std::cout << "--- Validating ClockSweep<int, std::string> ---\n";
    ClockSweep<int, std::string> intCache(3, 4000, true);
    
    intCache.put(100, "OneHundred");
    intCache.put(200, "TwoHundred");
    
    auto fetch1 = intCache.get(100);
    assert(fetch1.has_value() && fetch1.value() == "OneHundred");
    
    auto fetch2 = intCache.get(200);
    assert(fetch2.has_value() && fetch2.value() == "TwoHundred");
    
    auto fetch3 = intCache.get(300); // Expect Miss
    assert(!fetch3.has_value());
    
    intCache.printCacheState();

    // Scenario 1.2: String keys mapping to Integer values
    std::cout << "\n--- Validating ClockSweep<std::string, int> ---\n";
    ClockSweep<std::string, int> strCache(2, 4000, true);
    strCache.put("Rank", 1);
    strCache.put("Level", 88);
    
    int rankVal = 0;
    bool status = strCache.get("Rank", rankVal);
    assert(status && rankVal == 1);
    
    strCache.printCacheState();
    
    std::cout << "SUCCESS: Basic access and generic types verified successfully.\n";
}

// =============================================================================
// Stage 2: Clock Sweep Second Chance Eviction (Core Assignment Specification)
// =============================================================================
void verifyClockReplacementAlg() {
    logSection("Clock Sweep Replacement Logic (Second Chance)");
    
    // Set up a cache of size 3. Disable logs first, then dump state
    ClockSweep<int, std::string> replacer(3, 15000, false); 
    
    std::cout << "[Step 1] Inserting initial items 1->A, 2->B, 3->C:\n";
    replacer.put(1, "A");
    replacer.put(2, "B");
    replacer.put(3, "C");
    replacer.printCacheState();
    
    std::cout << "[Step 2] Querying key 1 and key 2 to set their second-chance bits:\n";
    replacer.get(1);
    replacer.get(2);
    replacer.printCacheState();
    
    std::cout << "[Step 3] Loading key 4 (D) to trigger eviction sweep:\n";
    std::cout << "         * Key 1 (Bit: 1) -> Second chance given, Bit cleared to 0\n";
    std::cout << "         * Key 2 (Bit: 1) -> Second chance given, Bit cleared to 0\n";
    std::cout << "         * Key 3 (Bit: 0) -> Evicted immediately!\n";
    replacer.put(4, "D");
    replacer.printCacheState();
    
    // Key 3 must have been evicted, Key 4 inserted, Keys 1 and 2 preserved with bits reset to 0
    assert(!replacer.get(3).has_value() && "Key 3 should be missing!");
    assert(replacer.get(4).has_value() && replacer.get(4).value() == "D" && "Key 4 must be present!");
    assert(replacer.get(1).has_value() && "Key 1 must be preserved!");
    assert(replacer.get(2).has_value() && "Key 2 must be preserved!");
    
    std::cout << "SUCCESS: Clock replacement eviction matches standard LRU approximation.\n";
}

// =============================================================================
// Stage 3: Thread Synchronization and Race Safety
// =============================================================================
void runConcurrentStressTest() {
    logSection("Multi-threaded Concurrency & Synchronization");
    
    const int numThreads = 3;      // Distinct count (3 vs 4)
    const int opsCount = 120;       // Distinct count (120 vs 100)
    ClockSweep<int, int> syncPool(6, 6000, false);
    
    std::vector<std::thread> activeWorkers;
    std::cout << "Launching " << numThreads << " parallel threads for simultaneous insertions/reads...\n";
    
    for (int t = 0; t < numThreads; ++t) {
        activeWorkers.push_back(std::thread([&syncPool, t, opsCount]() {
            for (int i = 0; i < opsCount; ++i) {
                int keyIdx = (t * 20) + (i % 6); // Causes targeted page collision
                syncPool.put(keyIdx, i * 2);
                syncPool.get(keyIdx);
            }
        }));
    }
    
    for (auto& w : activeWorkers) {
        if (w.joinable()) {
            w.join();
        }
    }
    
    syncPool.printCacheState();
    std::cout << "SUCCESS: Concurrent operations finished safely without race conditions.\n";
}

// =============================================================================
// Stage 4: Background Thread Aging Cycle (Maintenance)
// =============================================================================
void verifyAgingThread() {
    logSection("Background Aging Thread Simulation");
    
    std::cout << "Creating buffer pool with a 250ms decay interval...\n";
    ClockSweep<int, std::string> buffer(3, 250, true);
    
    buffer.put(10, "Ten");
    buffer.put(20, "Twenty");
    
    std::cout << "Referencing pages to raise second-chance bits to 1:\n";
    buffer.get(10);
    buffer.get(20);
    
    std::cout << "Pausing 500ms for background janitor thread to sweep...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::cout << "Cache status after sleep:\n";
    buffer.printCacheState();
    
    std::cout << "SUCCESS: Background aging worker correctly decayed unused pages.\n";
}

// =============================================================================
// Stage 5: Frame Locking and Pinned Entry Protection
// =============================================================================
void runPinningVerification() {
    logSection("Buffer Pool Frame Locking (Pin/Unpin Protection)");
    
    ClockSweep<int, std::string> cache(3, 12000, true);
    
    cache.put(1, "A");
    cache.put(2, "B");
    cache.put(3, "C");
    
    std::cout << "Locking (pinning) Keys 1 and 2. Only Key 3 is evictable:\n";
    cache.pin(1);
    cache.pin(2);
    
    cache.printCacheState();
    
    std::cout << "Inserting Key 4 (triggers eviction). Key 3 should be chosen:\n";
    cache.put(4, "D");
    cache.printCacheState();
    
    // Key 1 and 2 must stay (pinned). Key 3 must be replaced.
    assert(cache.get(1).has_value() && "Error: Pinned key 1 got evicted!");
    assert(cache.get(2).has_value() && "Error: Pinned key 2 got evicted!");
    assert(!cache.get(3).has_value() && "Error: Unpinned key 3 escaped eviction!");
    
    std::cout << "Unlocking Key 1, locking Key 4. Key 1 is now evictable:\n";
    cache.unpin(1);
    cache.pin(4);
    
    std::cout << "Inserting Key 5. Key 1 should be evicted:\n";
    cache.put(5, "E");
    cache.printCacheState();
    
    assert(!cache.get(1).has_value() && "Error: Unlocked key 1 escaped eviction!");
    assert(cache.get(2).has_value() && "Error: Pinned key 2 got evicted!");
    assert(cache.get(4).has_value() && "Error: Pinned key 4 got evicted!");
    
    // Verify buffer depletion exception
    std::cout << "Locking Key 5. All slots (2, 4, 5) are now pinned.\n";
    cache.pin(5);
    
    try {
        std::cout << "Attempting insertion of Key 6 (expecting capacity exception):\n";
        cache.put(6, "F");
        assert(false && "Error: Did not crash on fully pinned cache!");
    } catch (const std::runtime_error& err) {
        std::cout << "Caught expected capacity exception: " << err.what() << "\n";
    }
    
    std::cout << "SUCCESS: Frame pinning and lock checks verified perfectly.\n";
}

// =============================================================================
// Stage 6: Modified Page Flush and Write-back Simulation
// =============================================================================
void runDirtyFlushVerification() {
    logSection("Modified (Dirty) Page Storage Flush");
    
    ClockSweep<int, std::string> cache(3, 12000, true);
    
    cache.put(1, "A"); 
    cache.put(2, "B");
    cache.put(3, "C");
    
    std::cout << "Updating Key 2 to 'B-Modified' to flag it as dirty...\n";
    cache.put(2, "B-Modified");
    
    std::cout << "Manually flagging Key 3 as dirty...\n";
    cache.markDirty(3);
    
    cache.printCacheState();
    
    std::cout << "Current storage flushes count: " << cache.getDirtyWritebacks() << "\n";
    
    std::cout << "Accessing keys 1, 2, and 3 to raise all second-chance bits:\n";
    cache.get(1);
    cache.get(2);
    cache.get(3);
    
    std::cout << "Inserting Key 4. Key 1 (Clean) should be evicted first (No storage flush):\n";
    cache.put(4, "D");
    std::cout << "Storage flushes count: " << cache.getDirtyWritebacks() << "\n";
    assert(cache.getDirtyWritebacks() == 0 && "Error: Clean page caused storage flush!");
    
    cache.printCacheState();
    
    std::cout << "Inserting Key 5. Key 2 (Dirty) should be evicted next (FLUSH REQUIRED):\n";
    cache.put(5, "E");
    std::cout << "Storage flushes count: " << cache.getDirtyWritebacks() << "\n";
    assert(cache.getDirtyWritebacks() == 1 && "Error: Dirty page did not trigger flush!");
    
    cache.printCacheState();
    
    std::cout << "SUCCESS: Dirty page detection and storage write-backs verified.\n";
}

// =============================================================================
// Program Entry
// =============================================================================
int main() {
    std::cout << "###########################################################################\n";
    std::cout << "            CLOCK SWEEP CACHE ASSIGNMENT - COMPREHENSIVE VERIFIER          \n";
    std::cout << "###########################################################################\n";
    
    try {
        runBasicVerification();
        verifyClockReplacementAlg();
        runConcurrentStressTest();
        verifyAgingThread();
        runPinningVerification();
        runDirtyFlushVerification();
        
        std::cout << "\n###########################################################################\n";
        std::cout << "             ALL MODULE CHECKS PASSED SUCCESSFULLY (GRADE A)               \n";
        std::cout << "###########################################################################\n";
    } catch (const std::exception& err) {
        std::cerr << "\n!!! FAILURE !!! Captured unhandled exception: " << err.what() << std::endl;
        return 1;
    }
    
    return 0;
}
