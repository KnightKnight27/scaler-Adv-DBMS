// Smoke test for M1 (storage engine). Verifies: page allocation, write/read
// round-trip through the buffer pool, persistence across a reopen, and that
// clock-sweep eviction actually evicts when the pool is full of unpinned pages.
#include "buffer_pool.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace minidb;

int main() {
    const std::string dbf = "minidb_test.db";
    std::remove(dbf.c_str());

    // --- write a few pages, remembering what we put on each -----------------
    const int N = 200;  // far more than a 16-frame pool -> forces eviction
    {
        DiskManager dm(dbf);
        BufferPoolManager bpm(16, &dm);

        page_id_t first = INVALID_PAGE_ID;
        for (int i = 0; i < N; ++i) {
            page_id_t pid;
            Page* p = bpm.new_page(&pid);
            assert(p != nullptr && "pool should always find a frame here");
            if (i == 0) first = pid;
            std::snprintf(p->data, PAGE_SIZE, "page-%d-payload", pid);
            bpm.unpin_page(pid, /*dirty=*/true);  // immediately evictable
        }
        // first page was long ago evicted; fetch must read it back from disk.
        Page* p = bpm.fetch_page(first);
        assert(p != nullptr);
        assert(std::string(p->data) == "page-0-payload");
        bpm.unpin_page(first, false);
        bpm.flush_all();
        std::printf("[M1] wrote %d pages, eviction + refetch OK\n", N);
    }

    // --- reopen the file: data must survive (durability) --------------------
    {
        DiskManager dm(dbf);
        BufferPoolManager bpm(16, &dm);
        Page* p = bpm.fetch_page(150);
        assert(p != nullptr);
        assert(std::string(p->data) == "page-150-payload");
        bpm.unpin_page(150, false);
        std::printf("[M1] data survived reopen (page 150 = '%s')\n", p->data);
    }

    std::remove(dbf.c_str());
    std::printf("[M1] storage engine: ALL CHECKS PASSED\n");
    return 0;
}
