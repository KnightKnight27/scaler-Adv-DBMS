#include "storage.h"
#include "buffer_pool.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

// ---------- helpers ----------

static const std::string DB_FILE = "/tmp/test_solo_engine.db";

static void cleanup() {
    std::filesystem::remove(DB_FILE);
}

static void fill(char *buf, char value) {
    std::memset(buf, value, PAGE_SIZE);
}

static bool all_eq(const char *buf, char value) {
    for (size_t i = 0; i < PAGE_SIZE; ++i) {
        if (buf[i] != value) return false;
    }
    return true;
}

// ---------- DiskManager tests ----------

static void test_disk_allocate_and_write() {
    cleanup();
    DiskManager dm(DB_FILE);

    page_id_t p0 = dm.AllocatePage();
    page_id_t p1 = dm.AllocatePage();
    assert(p0 == 0);
    assert(p1 == 1);
    assert(dm.GetNumPages() == 2);

    char buf[PAGE_SIZE];
    fill(buf, 0xAA);
    dm.WritePage(p0, buf);
    fill(buf, 0xBB);
    dm.WritePage(p1, buf);

    std::cout << "[PASS] test_disk_allocate_and_write\n";
}

static void test_disk_read_back() {
    // Re-open the file written by the previous test.
    DiskManager dm(DB_FILE);
    assert(dm.GetNumPages() == 2);

    char buf[PAGE_SIZE];
    dm.ReadPage(0, buf);
    assert(all_eq(buf, 0xAA));

    dm.ReadPage(1, buf);
    assert(all_eq(buf, 0xBB));

    std::cout << "[PASS] test_disk_read_back\n";
}

static void test_disk_overwrite() {
    DiskManager dm(DB_FILE);

    char buf[PAGE_SIZE];
    fill(buf, 0xCC);
    dm.WritePage(0, buf);

    dm.ReadPage(0, buf);
    assert(all_eq(buf, 0xCC));

    // Page 1 should be unchanged.
    dm.ReadPage(1, buf);
    assert(all_eq(buf, 0xBB));

    std::cout << "[PASS] test_disk_overwrite\n";
}

// ---------- BufferPoolManager tests ----------

static void test_bpm_new_and_fetch() {
    cleanup();
    DiskManager dm(DB_FILE);
    BufferPoolManager bpm(4, &dm);

    page_id_t pid;
    Page *p = bpm.NewPage(pid);
    assert(p != nullptr);
    assert(pid == 0);

    fill(p->data, 0x11);
    bpm.UnpinPage(pid, /*is_dirty=*/true);

    // Fetch it back (should already be in pool).
    Page *fetched = bpm.FetchPage(pid);
    assert(fetched != nullptr);
    assert(all_eq(fetched->data, 0x11));
    bpm.UnpinPage(pid, false);

    std::cout << "[PASS] test_bpm_new_and_fetch\n";
}

static void test_bpm_flush() {
    cleanup();
    DiskManager dm(DB_FILE);
    {
        BufferPoolManager bpm(4, &dm);
        page_id_t pid;
        Page *p = bpm.NewPage(pid);
        fill(p->data, 0x22);
        bpm.UnpinPage(pid, true);
        bpm.FlushPage(pid);
    }
    // Re-read from disk directly.
    char buf[PAGE_SIZE];
    dm.ReadPage(0, buf);
    assert(all_eq(buf, 0x22));

    std::cout << "[PASS] test_bpm_flush\n";
}

// Pool of size 3, allocate 4 pages — the 4th allocation must evict one page.
static void test_bpm_lru_eviction_basic() {
    cleanup();
    DiskManager dm(DB_FILE);
    BufferPoolManager bpm(3, &dm);

    page_id_t pids[4];
    Page *pages[4];

    // Fill the pool.
    for (int i = 0; i < 3; ++i) {
        pages[i] = bpm.NewPage(pids[i]);
        assert(pages[i] != nullptr);
        fill(pages[i]->data, static_cast<char>(0x10 + i));
    }

    // Unpin all — now all are eviction candidates.
    for (int i = 0; i < 3; ++i) {
        bpm.UnpinPage(pids[i], true);
    }

    // Access page 0 and page 1 so page 2 becomes the LRU victim.
    assert(bpm.FetchPage(pids[0]) != nullptr);
    bpm.UnpinPage(pids[0], false);
    assert(bpm.FetchPage(pids[1]) != nullptr);
    bpm.UnpinPage(pids[1], false);

    // Allocating a 4th page must evict page 2 (LRU).
    Page *p3 = bpm.NewPage(pids[3]);
    assert(p3 != nullptr);
    bpm.UnpinPage(pids[3], false);

    // page 2 must have been written to disk (dirty) before eviction.
    char buf[PAGE_SIZE];
    dm.ReadPage(pids[2], buf);
    assert(all_eq(buf, 0x12)); // 0x10 + 2

    std::cout << "[PASS] test_bpm_lru_eviction_basic\n";
}

// Pinned pages must NOT be evicted.
static void test_bpm_pinned_pages_not_evicted() {
    cleanup();
    DiskManager dm(DB_FILE);
    const size_t POOL = 2;
    BufferPoolManager bpm(POOL, &dm);

    page_id_t p0id, p1id, p2id;
    assert(bpm.NewPage(p0id) != nullptr);
    assert(bpm.NewPage(p1id) != nullptr);
    // Both frames occupied and PINNED — pool is full with no evictable frame.

    assert(bpm.NewPage(p2id) == nullptr); // should fail: nothing to evict

    // Unpin p0, now it becomes evictable.
    bpm.UnpinPage(p0id, false);
    assert(bpm.NewPage(p2id) != nullptr);

    bpm.UnpinPage(p1id, false);
    bpm.UnpinPage(p2id, false);

    std::cout << "[PASS] test_bpm_pinned_pages_not_evicted\n";
}

// Dirty pages written to disk automatically when evicted.
static void test_bpm_dirty_written_on_eviction() {
    cleanup();
    DiskManager dm(DB_FILE);
    BufferPoolManager bpm(1, &dm); // single-frame pool — every new page evicts the previous

    page_id_t pid0, pid1;
    Page *p0 = bpm.NewPage(pid0); assert(p0 != nullptr);
    fill(p0->data, 0xDD);
    bpm.UnpinPage(pid0, /*dirty=*/true);

    // This evicts pid0 (dirty) — must flush it first.
    assert(bpm.NewPage(pid1) != nullptr);
    bpm.UnpinPage(pid1, false);

    char buf[PAGE_SIZE];
    dm.ReadPage(pid0, buf);
    assert(all_eq(buf, 0xDD));

    std::cout << "[PASS] test_bpm_dirty_written_on_eviction\n";
}

// LRU order: access pages in sequence, verify oldest is evicted.
static void test_bpm_lru_order() {
    cleanup();
    DiskManager dm(DB_FILE);
    const size_t POOL = 3;
    BufferPoolManager bpm(POOL, &dm);

    page_id_t ids[3];
    for (int i = 0; i < 3; ++i) {
        Page *p = bpm.NewPage(ids[i]);
        assert(p != nullptr);
        fill(p->data, static_cast<char>(0xA0 + i));
        bpm.UnpinPage(ids[i], true);
    }
    // Access order: ids[1], ids[2], ids[0] — making ids[1] the most recent
    // and leaving ids[0] as the oldest (LRU) after the subsequent accesses.
    // Sequence of touches to establish order (oldest->newest): ids[0], ids[2], ids[1]
    //   touch ids[2]
    assert(bpm.FetchPage(ids[2]) != nullptr); bpm.UnpinPage(ids[2], false);
    //   touch ids[1]
    assert(bpm.FetchPage(ids[1]) != nullptr); bpm.UnpinPage(ids[1], false);
    // LRU order (back→front of eviction): ids[0] < ids[2] < ids[1]
    // Next allocation should evict ids[0].
    page_id_t newid;
    assert(bpm.NewPage(newid) != nullptr);
    bpm.UnpinPage(newid, false);

    // ids[0] (dirty) should have been written to disk.
    char buf[PAGE_SIZE];
    dm.ReadPage(ids[0], buf);
    assert(all_eq(buf, 0xA0));

    // ids[1] and ids[2] should still be in the pool (fetchable without I/O re-read).
    assert(bpm.FetchPage(ids[1]) != nullptr); bpm.UnpinPage(ids[1], false);
    assert(bpm.FetchPage(ids[2]) != nullptr); bpm.UnpinPage(ids[2], false);

    std::cout << "[PASS] test_bpm_lru_order\n";
}

// ---------- main ----------

int main() {
    std::cout << "=== DiskManager Tests ===\n";
    test_disk_allocate_and_write();
    test_disk_read_back();
    test_disk_overwrite();

    std::cout << "\n=== BufferPoolManager Tests ===\n";
    test_bpm_new_and_fetch();
    test_bpm_flush();
    test_bpm_lru_eviction_basic();
    test_bpm_pinned_pages_not_evicted();
    test_bpm_dirty_written_on_eviction();
    test_bpm_lru_order();

    cleanup();
    std::cout << "\nAll tests passed.\n";
    return 0;
}
