#include "../src/page.h"
#include "../src/heap_file.h"
#include "../src/buffer_pool.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <thread>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

void test_heap_file_basics() {
    std::cout << "[RUN] test_heap_file_basics\n";
    std::string filename = "test_heap_file.dat";
    if (fs::exists(filename)) {
        fs::remove(filename);
    }

    {
        HeapFile hf(filename);
        int p0 = hf.allocatePage();
        int p1 = hf.allocatePage();
        assert(p0 == 0);
        assert(p1 == 1);

        Page page0;
        std::strcpy(page0.data, "Data on page 0");
        hf.writePage(p0, &page0);

        Page page1;
        std::strcpy(page1.data, "Data on page 1");
        hf.writePage(p1, &page1);
    } // Destructor closes file

    // Reopen and read to verify persistence
    {
        HeapFile hf(filename);
        Page page0;
        hf.readPage(0, &page0);
        assert(std::strcmp(page0.data, "Data on page 0") == 0);

        Page page1;
        hf.readPage(1, &page1);
        assert(std::strcmp(page1.data, "Data on page 1") == 0);
    }

    fs::remove(filename);
    std::cout << "[PASS] test_heap_file_basics\n";
}

void test_buffer_pool_basics() {
    std::cout << "[RUN] test_buffer_pool_basics\n";
    std::string filename = "test_buffer_pool.dat";
    if (fs::exists(filename)) {
        fs::remove(filename);
    }

    {
        HeapFile hf(filename);
        BufferPool bp(3, &hf); // Pool size 3

        // Allocate 4 pages on disk
        int p0 = hf.allocatePage();
        int p1 = hf.allocatePage();
        int p2 = hf.allocatePage();
        int p3 = hf.allocatePage();

        // Get page 0, write data, unpin (mark dirty)
        Page* pg0 = bp.getPage(p0);
        assert(pg0 != nullptr);
        std::strcpy(pg0->data, "Page 0 Content");
        bp.unpinPage(p0, true); // Pin count = 0, is_dirty = true

        // Get page 1, write data, unpin (mark dirty)
        Page* pg1 = bp.getPage(p1);
        assert(pg1 != nullptr);
        std::strcpy(pg1->data, "Page 1 Content");
        bp.unpinPage(p1, true);

        // Get page 2, write data, unpin (mark dirty)
        Page* pg2 = bp.getPage(p2);
        assert(pg2 != nullptr);
        std::strcpy(pg2->data, "Page 2 Content");
        bp.unpinPage(p2, true);

        // Current LRU order: p2 (front), p1, p0 (back)
        // Access p0 to make it recently used
        Page* pg0_again = bp.getPage(p0);
        assert(pg0_again != nullptr);
        assert(std::strcmp(pg0_again->data, "Page 0 Content") == 0);
        bp.unpinPage(p0, false); // pin_count goes back to 0, stays dirty

        // LRU order: p0 (front), p2, p1 (back)
        // Now get p3. This causes a cache miss and should evict p1 (back of LRU list)
        Page* pg3 = bp.getPage(p3);
        assert(pg3 != nullptr);
        std::strcpy(pg3->data, "Page 3 Content");
        bp.unpinPage(p3, true);

        // Since p1 was evicted and it was dirty, it should have been written to disk.
        // Let's get page 1 again. It should be loaded from disk.
        Page* pg1_again = bp.getPage(p1);
        assert(pg1_again != nullptr);
        assert(std::strcmp(pg1_again->data, "Page 1 Content") == 0);
        bp.unpinPage(p1, false);
    }

    fs::remove(filename);
    std::cout << "[PASS] test_buffer_pool_basics\n";
}

void test_buffer_pool_concurrency() {
    std::cout << "[RUN] test_buffer_pool_concurrency\n";
    std::string filename = "test_buffer_pool_concurrent.dat";
    if (fs::exists(filename)) {
        fs::remove(filename);
    }

    {
        HeapFile hf(filename);
        BufferPool bp(5, &hf); // Pool size 5

        // Allocate 10 pages
        std::vector<int> pids;
        for (int i = 0; i < 10; ++i) {
            pids.push_back(hf.allocatePage());
        }

        // Spawn threads that concurrently get, modify, unpin pages
        auto worker = [&](int thread_id) {
            for (int step = 0; step < 50; ++step) {
                int page_idx = (thread_id + step) % 10;
                int pid = pids[page_idx];

                Page* pg = bp.getPage(pid);
                if (pg != nullptr) {
                    // Simulating page write
                    std::string msg = "T" + std::to_string(thread_id) + "S" + std::to_string(step);
                    std::strncpy(pg->data, msg.c_str(), PAGE_SIZE - 1);
                    bp.unpinPage(pid, true);
                }
            }
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back(worker, i);
        }

        for (auto& t : threads) {
            t.join();
        }
    }

    fs::remove(filename);
    std::cout << "[PASS] test_buffer_pool_concurrency\n";
}

int main() {
    try {
        test_heap_file_basics();
        test_buffer_pool_basics();
        test_buffer_pool_concurrency();
        std::cout << "\nAll Track 2 tests PASSED successfully!\n";
    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED with exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
