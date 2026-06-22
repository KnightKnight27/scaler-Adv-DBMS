#include "buffer_pool.h"
#include <stdexcept>

BufferPool::BufferPool(PageManager& pm, int pool_size)
    : pm(pm), pool_size(pool_size), frames(pool_size), clock_hand(0) {}

// Linear scan to find which frame holds page_id.
int BufferPool::findFrame(int page_id) {
    for (int i = 0; i < pool_size; i++) {
        if (frames[i].page_id == page_id) return i;
    }
    return -1;
}

// Clock Sweep: rotate the clock hand until we find a frame with
// usage_count == 0 that is not pinned. Decrement usage_count as we pass.
int BufferPool::evict() {
    while (true) {
        Frame& f = frames[clock_hand];
        if (!f.is_pinned) {
            if (f.usage_count == 0) {
                int victim = clock_hand;
                clock_hand = (clock_hand + 1) % pool_size;
                return victim;
            }
            f.usage_count--;
        }
        clock_hand = (clock_hand + 1) % pool_size;
    }
}

Page* BufferPool::fetchPage(int page_id) {
    // Cache hit: page already in pool.
    int idx = findFrame(page_id);
    if (idx != -1) {
        frames[idx].is_pinned   = true;
        frames[idx].usage_count = 5; // refresh
        return &frames[idx].page;
    }

    // Cache miss: need to load from disk.
    idx = evict();
    Frame& f = frames[idx];

    // Write the evicted page back to disk if it was modified.
    if (f.is_dirty && f.page_id != -1) {
        pm.writePage(f.page_id, f.page);
    }

    // Load the requested page into this frame.
    pm.readPage(page_id, f.page);
    f.page_id     = page_id;
    f.is_dirty    = false;
    f.is_pinned   = true;
    f.usage_count = 5;

    return &f.page;
}

void BufferPool::unpinPage(int page_id, bool dirty) {
    int idx = findFrame(page_id);
    if (idx == -1) return;
    frames[idx].is_pinned = false;
    if (dirty) frames[idx].is_dirty = true;
}

void BufferPool::flushAll() {
    for (Frame& f : frames) {
        if (f.is_dirty && f.page_id != -1) {
            pm.writePage(f.page_id, f.page);
            f.is_dirty = false;
        }
    }
}
