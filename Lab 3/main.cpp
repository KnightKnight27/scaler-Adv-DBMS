#include "ClockSweep.hpp"
#include <iostream>
#include <string>
#include <cassert>
#include <thread>
#include <vector>
#include <chrono>

// Utility function to print beautiful headers
void printTestHeader(const std::string& name) {
    std::cout << "\n" << std::string(80, '*') << "\n";
    std::cout << " RUNNING TEST: " << name << "\n";
    std::cout << std::string(80, '*') << "\n";
}

// -----------------------------------------------------------------------------
// Test Case 1: Basic Operations & Template Generics
// -----------------------------------------------------------------------------
void testBasicOperations() {
    printTestHeader("Basic Operations & Template Generics");

    // Test with <int, std::string>
    std::cout << "-> Testing with ClockSweep<int, std::string>...\n";
    ClockSweep<int, std::string> intCache(3, 5000, true);
    
    intCache.put(1, "ValueOne");
    intCache.put(2, "ValueTwo");
    
    auto val1 = intCache.get(1);
    assert(val1.has_value() && val1.value() == "ValueOne");
    
    auto val2 = intCache.get(2);
    assert(val2.has_value() && val2.value() == "ValueTwo");
    
    auto val3 = intCache.get(3); // Miss
    assert(!val3.has_value());
    
    intCache.printCacheState();

    // Test with <std::string, int>
    std::cout << "\n-> Testing with ClockSweep<std::string, int>...\n";
    ClockSweep<std::string, int> strCache(2, 5000, true);
    strCache.put("Age", 20);
    strCache.put("Score", 99);
    
    int score = 0;
    bool found = strCache.get("Score", score);
    assert(found && score == 99);
    
    strCache.printCacheState();
    
    std::cout << "SUCCESS: Basic Operations & Template Generics completed successfully!\n";
}

// -----------------------------------------------------------------------------
// Test Case 2: Verification of the Clock Sweep Eviction Algorithm (Section 11)
// -----------------------------------------------------------------------------
void testClockSweepEviction() {
    printTestHeader("Clock Sweep Eviction Algorithm Verification (Section 11)");
    
    // Set up a cache of size 3. Disable debug logs to keep it clean, then print states
    ClockSweep<int, std::string> cache(3, 10000, false); 
    
    std::cout << "1. Inserting three items into cache (Size 3):\n";
    cache.put(1, "A");
    cache.put(2, "B");
    cache.put(3, "C");
    cache.printCacheState();
    
    std::cout << "2. Accessing key 1 and key 2 (Reference bits become 1):\n";
    cache.get(1);
    cache.get(2);
    cache.printCacheState();
    
    std::cout << "3. Inserting key 4 (Value 'D') which triggers eviction:\n";
    std::cout << "   - Key 1 (Ref: 1) -> Given second chance, Ref reset to 0\n";
    std::cout << "   - Key 2 (Ref: 1) -> Given second chance, Ref reset to 0\n";
    std::cout << "   - Key 3 (Ref: 0) -> Evicted!\n";
    cache.put(4, "D");
    cache.printCacheState();
    
    // Key 3 should be evicted, key 4 inserted, keys 1 and 2 reference bits cleared to 0
    assert(!cache.get(3).has_value() && "Key 3 should have been evicted!");
    assert(cache.get(4).has_value() && cache.get(4).value() == "D" && "Key 4 must exist!");
    assert(cache.get(1).has_value() && "Key 1 must still exist!");
    assert(cache.get(2).has_value() && "Key 2 must still exist!");
    
    std::cout << "SUCCESS: Clock Sweep Eviction matched the exact expected behavior!\n";
}

// -----------------------------------------------------------------------------
// Test Case 3: Thread-Safety & Concurrent Access
// -----------------------------------------------------------------------------
void testMultithreading() {
    printTestHeader("Thread Safety & Multi-threaded Operations");
    
    const int threadCount = 4;
    const int opsPerThread = 100;
    ClockSweep<int, int> concurrentCache(5, 5000, false);
    
    std::vector<std::thread> workers;
    
    std::cout << "Spawning " << threadCount << " writer & reader threads to verify synchronization...\n";
    
    for (int t = 0; t < threadCount; ++t) {
        workers.push_back(std::thread([&concurrentCache, t, opsPerThread]() {
            for (int i = 0; i < opsPerThread; ++i) {
                // Perform a mix of puts and gets
                int key = (t * 10) + (i % 8); // Causes key collisions across threads to trigger updates
                concurrentCache.put(key, i);
                concurrentCache.get(key);
            }
        }));
    }
    
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    concurrentCache.printCacheState();
    std::cout << "SUCCESS: Multi-threaded test completed. Cache is stable under concurrent pressure.\n";
}

// -----------------------------------------------------------------------------
// Test Case 4: Background Thread Page Aging
// -----------------------------------------------------------------------------
void testBackgroundSweeper() {
    printTestHeader("Background Thread Reference Bit Aging (Maintenance)");
    
    // Set a very short sweep interval of 300ms for fast testing
    std::cout << "Initializing cache with a 300ms background sweep interval...\n";
    ClockSweep<int, std::string> cache(3, 300, true);
    
    cache.put(1, "A");
    cache.put(2, "B");
    
    std::cout << "Accessing 1 and 2 to set their reference bits to 1:\n";
    cache.get(1);
    cache.get(2);
    
    std::cout << "Waiting 600ms for background sweeper to run...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    
    // After 600ms, the background thread should have cleared the reference bits to 0
    std::cout << "Current cache state after waiting:\n";
    cache.printCacheState();
    
    std::cout << "SUCCESS: Background page aging demonstrated.\n";
}

// -----------------------------------------------------------------------------
// Test Case 5: Database Buffer Pinning & Eviction Prevention
// -----------------------------------------------------------------------------
void testPinningAndEviction() {
    printTestHeader("Database Buffer Pool Pinning (Eviction Prevention)");
    
    ClockSweep<int, std::string> cache(3, 10000, true);
    
    cache.put(1, "A");
    cache.put(2, "B");
    cache.put(3, "C");
    
    std::cout << "Pinning Key 1 (A) and Key 2 (B). Only Key 3 (C) can be evicted!\n";
    cache.pin(1);
    cache.pin(2);
    
    cache.printCacheState();
    
    std::cout << "Inserting Key 4 (D) which triggers eviction. Key 3 should be evicted:\n";
    cache.put(4, "D");
    cache.printCacheState();
    
    // Key 1 and 2 must still be present because they are pinned. Key 3 is evicted.
    assert(cache.get(1).has_value() && "Key 1 (Pinned) was incorrectly evicted!");
    assert(cache.get(2).has_value() && "Key 2 (Pinned) was incorrectly evicted!");
    assert(!cache.get(3).has_value() && "Key 3 (Unpinned) should have been evicted!");
    
    std::cout << "Unpinning Key 1 and pinning Key 4. Now Key 1 can be evicted:\n";
    cache.unpin(1);
    cache.pin(4);
    
    std::cout << "Inserting Key 5 (E). Key 1 should be evicted:\n";
    cache.put(5, "E");
    cache.printCacheState();
    
    assert(!cache.get(1).has_value() && "Key 1 (Unpinned) should have been evicted!");
    assert(cache.get(2).has_value() && "Key 2 (Pinned) must still exist!");
    assert(cache.get(4).has_value() && "Key 4 (Pinned) must still exist!");
    
    // Try to trigger a Buffer Pool Full Exception by pinning all frames
    std::cout << "Pinning Key 5. Now all 3 frames (2, 4, 5) are pinned.\n";
    cache.pin(5);
    
    try {
        std::cout << "Attempting to insert Key 6 (F). This should throw an exception:\n";
        cache.put(6, "F");
        assert(false && "Should have thrown a buffer full exception!");
    } catch (const std::runtime_error& e) {
        std::cout << "Caught expected exception: " << e.what() << "\n";
    }
    
    std::cout << "SUCCESS: Pinning and eviction prevention verified successfully!\n";
}

// -----------------------------------------------------------------------------
// Test Case 6: Dirty Page Write-back Simulation
// -----------------------------------------------------------------------------
void testDirtyPageWriteback() {
    printTestHeader("Dirty Page Write-back Simulation");
    
    ClockSweep<int, std::string> cache(3, 10000, true);
    
    cache.put(1, "A"); // Clean insertion
    cache.put(2, "B");
    cache.put(3, "C");
    
    // Modify Key 2, marking it dirty
    std::cout << "Updating Key 2 to 'B-Modified' to mark it dirty...\n";
    cache.put(2, "B-Modified");
    
    // Explicitly mark Key 3 dirty
    std::cout << "Explicitly marking Key 3 as dirty...\n";
    cache.markDirty(3);
    
    cache.printCacheState();
    
    // Now make Key 1 and Key 2 referenced, leaving Key 3 (Dirty) as the first victim candidate
    // Oh wait, let's keep clock hand at 0.
    // If we insert key 4, we want to see dirty write-back.
    // Let's run eviction. Key 1 has ref = 1 (since we just inserted it, wait. All insertions put ref = 1).
    // Let's print stats before and after eviction.
    std::cout << "Current Dirty Write-backs count: " << cache.getDirtyWritebacks() << "\n";
    
    std::cout << "Accessing Key 1, Key 2, and Key 3 to set all their reference bits to 1:\n";
    cache.get(1);
    cache.get(2);
    cache.get(3); // Explicitly reference Key 3 so it has referenceBit = 1
    // Now all frames have Ref: 1. Let's see:
    // If we call put(4, "D"), it sweeps clockHand (initially 0).
    // At index 0 (Key 1): Ref 1 -> 0, clockHand moves to 1.
    // At index 1 (Key 2): Ref 1 -> 0, clockHand moves to 2.
    // At index 2 (Key 3): Ref 1 -> 0, clockHand moves to 0.
    // At index 0 (Key 1): Ref 0 -> Evict! (Since Key 1 is clean, it should be evicted with NO write-back)
    std::cout << "Inserting Key 4. Key 1 (Clean) should be evicted first. No write-back should occur.\n";
    cache.put(4, "D");
    std::cout << "Dirty Write-backs count: " << cache.getDirtyWritebacks() << "\n";
    assert(cache.getDirtyWritebacks() == 0 && "Clean page eviction should not increment writeback count!");
    
    cache.printCacheState();
    
    // Now, clockHand is at 1 (Key 2, Ref: 0, Dirty: 1).
    // Let's insert Key 5. Key 2 (Dirty) should be evicted, triggering a write-back!
    std::cout << "Inserting Key 5. Key 2 (Dirty) should be evicted. A write-back SHOULD occur:\n";
    cache.put(5, "E");
    std::cout << "Dirty Write-backs count: " << cache.getDirtyWritebacks() << "\n";
    assert(cache.getDirtyWritebacks() == 1 && "Dirty page eviction must increment writeback count!");
    
    cache.printCacheState();
    
    std::cout << "SUCCESS: Dirty Page Write-back behaves exactly as expected!\n";
}

// -----------------------------------------------------------------------------
// Main Function
// -----------------------------------------------------------------------------
int main() {
    std::cout << "=========================================================================\n";
    std::cout << "              CLOCK SWEEP CACHE ASSIGNMENT - TEST RUNNER                 \n";
    std::cout << "=========================================================================\n";
    
    try {
        testBasicOperations();
        testClockSweepEviction();
        testMultithreading();
        testBackgroundSweeper();
        testPinningAndEviction();
        testDirtyPageWriteback();
        
        std::cout << "\n=========================================================================\n";
        std::cout << "               ALL CLOCK SWEEP CACHE TESTS PASSED SUCCESSFULLY!          \n";
        std::cout << "=========================================================================\n";
    } catch (const std::exception& e) {
        std::cerr << "\n!!! TEST FAILURE !!! Exception caught: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
