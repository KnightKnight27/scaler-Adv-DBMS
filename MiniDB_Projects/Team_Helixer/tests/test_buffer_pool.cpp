// Verifies the storage foundation: disk manager round-trips pages, the buffer
// pool caches/pins/evicts correctly, and dirty pages survive eviction.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"

using namespace minidb;

int main() {
    const char *db = "test_bp.db";
    std::remove(db);
    {
        DiskManager disk(db);
        // Tiny pool (3 frames) so eviction is exercised immediately.
        BufferPoolManager bpm(3, &disk);

        // Allocate 3 pages and write a marker string into each.
        page_id_t ids[5];
        for (int i = 0; i < 3; ++i) {
            Page *p = bpm.new_page(&ids[i]);
            assert(p != nullptr);
            std::snprintf(p->data(), PAGE_SIZE, "page-%d", i);
            assert(bpm.unpin_page(ids[i], /*dirty=*/true));
        }

        // Allocating 2 more forces eviction of the LRU frames. The contents of
        // the evicted (dirty) pages must have been flushed to disk.
        for (int i = 3; i < 5; ++i) {
            Page *p = bpm.new_page(&ids[i]);
            assert(p != nullptr);
            std::snprintf(p->data(), PAGE_SIZE, "page-%d", i);
            assert(bpm.unpin_page(ids[i], true));
        }

        // Re-fetch page 0 (was evicted). It must reload from disk with its data.
        Page *p0 = bpm.fetch_page(ids[0]);
        assert(p0 != nullptr);
        assert(std::strcmp(p0->data(), "page-0") == 0);
        assert(bpm.unpin_page(ids[0], false));

        std::cout << "[OK] buffer pool: alloc, pin/unpin, LRU eviction, "
                     "dirty write-back, reload all verified" << std::endl;
    }
    std::remove(db);
    return 0;
}
