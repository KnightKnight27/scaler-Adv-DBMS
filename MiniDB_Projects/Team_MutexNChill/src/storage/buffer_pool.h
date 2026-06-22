#pragma once
#include "page.h"
#include "page_manager.h"
#include <vector>

// One slot in the buffer pool. Holds one page loaded in memory.
struct Frame {
    int  page_id     = -1;    // which page is here (-1 = empty frame)
    bool is_dirty    = false; // was the page modified? needs write-back
    bool is_pinned   = false; // page is in use — do not evict
    int  usage_count = 0;     // clock sweep counter (counts down to 0)
    Page page;
};

// BufferPool keeps a small number of pages in memory so we avoid
// re-reading the same page from disk over and over.
//
// Eviction policy: Clock Sweep (same algorithm PostgreSQL uses).
// The "clock hand" sweeps around the pool; it decrements usage_count
// and evicts the first frame that reaches 0 and is not pinned.
class BufferPool {
public:
    BufferPool(PageManager& pm, int pool_size = 10);

    // Bring a page into memory and pin it (caller must call unpinPage when done).
    Page* fetchPage(int page_id);

    // Release the pin. Pass dirty=true if you modified the page.
    void  unpinPage(int page_id, bool dirty);

    // Write all dirty frames back to disk (called at shutdown).
    void  flushAll();

private:
    PageManager&       pm;
    int                pool_size;
    std::vector<Frame> frames;
    int                clock_hand;

    int findFrame(int page_id);  // returns frame index, or -1
    int evict();                 // returns index of a frame we can reuse
};
