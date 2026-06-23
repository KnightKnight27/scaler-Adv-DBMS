// Exercises the storage foundation end to end: page allocation, pin/unpin,
// LRU eviction with dirty write-back, and persistence across a reopen.

#include <cstdio>
#include <string>

#include "buffer/buffer_pool_manager.h"
#include "storage/disk_manager.h"
#include "tests/test_util.h"

using namespace minidb;

static std::string PageContent(int id) { return "page-" + std::to_string(id); }

int main() {
  const std::string file = "test_bpm.db";
  std::remove(file.c_str());

  // --- Phase 1: fill a tiny pool, force eviction, verify read-back ----------
  {
    DiskManager dm(file);
    BufferPoolManager bpm(/*pool_size=*/4, &dm);

    page_id_t ids[6];
    // Create 4 pages (fills the pool), write a marker, unpin dirty.
    for (int i = 0; i < 4; i++) {
      Page *p = bpm.NewPage(&ids[i]);
      CHECK(p != nullptr);
      std::snprintf(p->GetData(), PAGE_SIZE, "%s", PageContent(ids[i]).c_str());
      CHECK(bpm.UnpinPage(ids[i], /*is_dirty=*/true));
    }
    CHECK_EQ(ids[0], 0);
    CHECK_EQ(ids[3], 3);

    // Two more pages must evict (and flush) the two LRU frames.
    for (int i = 4; i < 6; i++) {
      Page *p = bpm.NewPage(&ids[i]);
      CHECK(p != nullptr);
      std::snprintf(p->GetData(), PAGE_SIZE, "%s", PageContent(ids[i]).c_str());
      CHECK(bpm.UnpinPage(ids[i], /*is_dirty=*/true));
    }

    // Page 0 was evicted to disk; fetching it must read the right bytes back.
    Page *p0 = bpm.FetchPage(0);
    CHECK(p0 != nullptr);
    CHECK(std::string(p0->GetData()) == PageContent(0));
    CHECK(bpm.UnpinPage(0, false));

    // A pinned page cannot be evicted: pin all 4 frames, next NewPage fails.
    page_id_t pinned[4];
    for (int i = 0; i < 4; i++) {
      Page *p = bpm.NewPage(&pinned[i]);
      CHECK(p != nullptr);  // leave pinned on purpose
    }
    page_id_t overflow;
    CHECK(bpm.NewPage(&overflow) == nullptr);  // pool exhausted, all pinned
    for (int i = 0; i < 4; i++) CHECK(bpm.UnpinPage(pinned[i], false));

    bpm.FlushAllPages();
  }

  // --- Phase 2: reopen the file; data must have survived --------------------
  {
    DiskManager dm(file);
    BufferPoolManager bpm(4, &dm);
    Page *p = bpm.FetchPage(3);
    CHECK(p != nullptr);
    CHECK(std::string(p->GetData()) == PageContent(3));
    CHECK(bpm.UnpinPage(3, false));
  }

  std::remove(file.c_str());
  return minidb::test::summary("buffer_pool");
}
