#include <thread>
#include <chrono>
#include <cstdio>
#include "ClockSweepCache.h"
#include "BufferTag.h"
#include "Page.h"

int main()
{
    using namespace std::chrono_literals;

    // cache holds 3 pages — just like postgres buffer pool but tiny
    ClockSweepCache<BufferTag, Page> cache(3);

    // simulate loading 3 pages from disk into cache
    BufferTag tag1 = {1, 1, 10, 0};  // tablespace=1, db=1, table=10, block=0
    BufferTag tag2 = {1, 1, 10, 1};  // same table, block=1
    BufferTag tag3 = {1, 1, 20, 0};  // different table

    cache.put(tag1, Page("row: id=1 name=alice"));
    cache.put(tag2, Page("row: id=2 name=bob"));
    cache.put(tag3, Page("row: id=3 name=charlie"));

    printf("--- initial pages loaded ---\n");

    // simulate queries hitting certain pages more
    // tag3 is a hot page — accessed 3 times
    for (int i = 0; i < 3; i++) {
        if (auto page = cache.get(tag3)) page->print();
    }

    // tag2 accessed once
    if (auto page2 = cache.get(tag2)) page2->print();

    // tag1 never accessed again — will decay and get evicted

    printf("\n--- waiting for background thread to evict cold pages ---\n");
    std::this_thread::sleep_for(3s);

    // now a new page comes in from disk — tag1 should already be gone
    BufferTag tag4 = {1, 1, 30, 0};  // new table
    printf("\n--- loading new page (table=30 block=0) ---\n");
    cache.put(tag4, Page("row: id=4 name=dave"));

    // one more — cache may be full, evicts least frequent
    BufferTag tag5 = {1, 1, 30, 1};
    printf("--- loading new page (table=30 block=1) ---\n");
    cache.put(tag5, Page("row: id=5 name=eve"));

    // verify hot page is still there
    printf("\n--- verifying hot page (table=20 block=0) still in cache ---\n");
    if (auto hotPage = cache.get(tag3))
        hotPage->print();
    else
        printf("hot page was evicted!\n");

    std::this_thread::sleep_for(1s);
    printf("\nDone\n");
    return 0;
}