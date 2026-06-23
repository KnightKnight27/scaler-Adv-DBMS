// Assert-based tests for the M1 storage layer: SlottedPage, DiskManager,
// BufferPool (clock-sweep + dirty write-back), and HeapFile.
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "common/types.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "storage/heap_file.h"
#include "storage/slotted_page.h"

using namespace minidb;

static int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

static void test_slotted_page() {
    std::vector<char> buf(PAGE_SIZE);
    SlottedPage sp(buf.data());
    sp.init();
    CHECK(sp.slot_count() == 0);
    std::uint16_t initial_free = sp.free_space();
    CHECK(initial_free > PAGE_SIZE - 64);  // almost the whole page

    std::int16_t s0, s1;
    CHECK(sp.insert("hello", 5, s0));
    CHECK(sp.insert("world!!", 7, s1));
    CHECK(s0 == 0 && s1 == 1);
    CHECK(sp.slot_count() == 2);

    std::string out;
    CHECK(sp.get(s0, out) && out == "hello");
    CHECK(sp.get(s1, out) && out == "world!!");

    // erase reuses the slot index on the next insert
    CHECK(sp.erase(s0));
    CHECK(!sp.get(s0, out));
    std::int16_t s2;
    CHECK(sp.insert("again", 5, s2));
    CHECK(s2 == 0);                        // dead slot 0 was reused
    CHECK(sp.get(s2, out) && out == "again");

    // fill the page and confirm insert eventually fails (free space respected)
    int inserted = 0;
    std::string big(200, 'x');
    std::int16_t s;
    while (sp.insert(big.data(), 200, s)) ++inserted;
    CHECK(inserted > 0);
    CHECK(sp.free_space() < 200 + SlottedPage::SLOT_SIZE);
    std::cout << "[ok] slotted_page (" << inserted << " 200B records fit)\n";
}

static void test_disk_manager() {
    const std::string path = "test_dm.db";
    std::remove(path.c_str());
    {
        DiskManager dm(path);
        PageId p0 = dm.allocate_page();
        PageId p1 = dm.allocate_page();
        CHECK(p0 == 0 && p1 == 1);
        CHECK(dm.num_pages() == 2);

        std::vector<char> page(PAGE_SIZE, 0);
        std::string msg = "persisted-page-content";
        std::memcpy(page.data() + 100, msg.data(), msg.size());
        dm.write_page(p1, page.data());
        dm.sync();
    }
    {
        // Reopen: data and page count must survive, checksum must verify.
        DiskManager dm(path);
        CHECK(dm.num_pages() == 2);
        std::vector<char> page(PAGE_SIZE, 0);
        dm.read_page(1, page.data());
        CHECK(std::string(page.data() + 100, 22) == "persisted-page-content");
    }
    std::remove(path.c_str());
    std::cout << "[ok] disk_manager (persistence + checksum)\n";
}

static void test_buffer_pool_eviction() {
    const std::string path = "test_bp.db";
    std::remove(path.c_str());
    DiskManager dm(path);
    BufferPool pool(/*frames=*/3, &dm);  // tiny pool to force eviction

    // Allocate and dirty 5 pages through a 3-frame pool -> evictions happen,
    // dirty pages get written back, and re-reading returns the data.
    std::vector<PageId> ids;
    for (int i = 0; i < 5; ++i) {
        PageId pid;
        Page* pg = pool.new_page(pid);
        pg->data()[0] = static_cast<char>('A' + i);  // note: offset 0 is checksum in real pages,
        pg->data()[10] = static_cast<char>('A' + i); //       use offset 10 for the actual marker
        pool.unpin_page(pid, /*dirty=*/true);
        ids.push_back(pid);
    }
    CHECK(pool.evictions() >= 2);  // 5 pages, 3 frames

    for (int i = 0; i < 5; ++i) {
        Page* pg = pool.fetch_page(ids[i]);
        CHECK(pg->data()[10] == static_cast<char>('A' + i));  // survived eviction/reload
        pool.unpin_page(ids[i], false);
    }
    std::remove(path.c_str());
    std::cout << "[ok] buffer_pool (evictions=" << pool.evictions()
              << " hits=" << pool.hits() << " misses=" << pool.misses() << ")\n";
}

static void test_heap_file() {
    const std::string path = "test_heap.db";
    std::remove(path.c_str());
    DiskManager dm(path);
    BufferPool pool(8, &dm);

    PageId first = HeapFile::create(&pool);
    HeapFile heap(&pool, first);

    const int N = 300;  // enough to span multiple pages
    std::vector<RID> rids;
    for (int i = 0; i < N; ++i)
        rids.push_back(heap.insert("record-number-" + std::to_string(i)));
    CHECK(dm.num_pages() > 1);  // chained across pages

    // point lookups by RID
    std::string out;
    CHECK(heap.get(rids[0], out) && out == "record-number-0");
    CHECK(heap.get(rids[N - 1], out) && out == "record-number-" + std::to_string(N - 1));

    // full scan visits every live record
    int count = 0;
    RID rid;
    for (auto it = heap.begin(); it.next(rid, out); ) ++count;
    CHECK(count == N);

    // erase then re-scan
    CHECK(heap.erase(rids[5]));
    CHECK(!heap.get(rids[5], out));
    count = 0;
    for (auto it = heap.begin(); it.next(rid, out); ) ++count;
    CHECK(count == N - 1);

    std::remove(path.c_str());
    std::cout << "[ok] heap_file (" << N << " rows over " << dm.num_pages() << " pages)\n";
}

int main() {
    test_slotted_page();
    test_disk_manager();
    test_buffer_pool_eviction();
    test_heap_file();
    if (g_failures == 0) {
        std::cout << "ALL STORAGE TESTS PASSED\n";
        return 0;
    }
    std::cerr << g_failures << " CHECK(s) FAILED\n";
    return 1;
}
