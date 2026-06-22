#include "catch.hpp"

#include <unistd.h>

#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "buffer/lru_k_evictor.h"
#include "storage/disk_storage_manager.h"
#include "storage/slotted_page_layout.h"

using namespace axiomdb;

namespace {
std::string temp_db_path(const char* tag) {
  return std::string("/tmp/axiomdb_test_") + tag + "_" + std::to_string(::getpid()) + ".wdb";
}
}  // namespace

TEST_CASE("LRU-K evicts <K-access frame before a hot one", "[buffer][lruk]") {
  LruKEvictor r(3, 2);
  // Frame 0 accessed twice (hot, finite distance); frames 1 and 2 once each.
  r.record_access(0);
  r.record_access(1);
  r.record_access(2);
  r.record_access(0);  // frame 0 now has 2 accesses
  r.set_evictable(0, true);
  r.set_evictable(1, true);
  r.set_evictable(2, true);
  REQUIRE(r.evictable_count() == 3);

  // Frames 1 and 2 have <K accesses -> infinite distance, evicted first, and
  // among them the one with the older first access (frame 1) goes first.
  REQUIRE(r.evict() == 1);
  REQUIRE(r.evict() == 2);
  // Now only the hot frame remains.
  REQUIRE(r.evict() == 0);
  REQUIRE_FALSE(r.evict().has_value());
}

TEST_CASE("LRU-K respects evictability (pinned frames are safe)", "[buffer][lruk]") {
  LruKEvictor r(2, 2);
  r.record_access(0);
  r.record_access(1);
  r.set_evictable(0, false);  // pinned
  r.set_evictable(1, true);
  REQUIRE(r.evict() == 1);
  REQUIRE_FALSE(r.evict().has_value());  // 0 is pinned, nothing to evict
}

TEST_CASE("BufferPoolManager flushes the dirty version on eviction", "[buffer][m1]") {
  std::string path = temp_db_path("bp_evict");
  ::remove(path.c_str());
  {
    DiskStorageManager dm(path);
    BufferPoolManager bp(&dm, /*pool_size=*/2, /*k=*/2);

    // Fill both frames with distinct, dirty pages.
    page_id_t p0, p1;
    Page* f0 = bp.new_page(&p0);
    Page* f1 = bp.new_page(&p1);
    REQUIRE(f0 != nullptr);
    REQUIRE(f1 != nullptr);
    {
      SlottedPageLayout s0(f0->data()); s0.init(); s0.insert("page-zero-data");
      SlottedPageLayout s1(f1->data()); s1.init(); s1.insert("page-one-data");
    }
    bp.unpin_page(p0, true);
    bp.unpin_page(p1, true);

    // Force eviction: fetching a third page must evict one of the two and
    // flush its (dirty) contents to disk first.
    page_id_t p2;
    Page* f2 = bp.new_page(&p2);
    REQUIRE(f2 != nullptr);
    bp.unpin_page(p2, true);

    bp.flush_all();
  }
  // Reopen and confirm both original pages persisted their *modified* content.
  {
    DiskStorageManager dm(path);
    std::array<char, PAGE_SIZE> buf{};
    REQUIRE(dm.read_page(0, buf.data()).ok());
    REQUIRE(SlottedPageLayout(buf.data()).get(0).value() == "page-zero-data");
    REQUIRE(dm.read_page(1, buf.data()).ok());
    REQUIRE(SlottedPageLayout(buf.data()).get(0).value() == "page-one-data");
  }
  ::remove(path.c_str());
}

TEST_CASE("BufferPoolManager returns nullptr when every frame is pinned", "[buffer]") {
  std::string path = temp_db_path("bp_pinned");
  ::remove(path.c_str());
  DiskStorageManager dm(path);
  BufferPoolManager bp(&dm, /*pool_size=*/2, /*k=*/2);

  page_id_t a, b;
  REQUIRE(bp.new_page(&a) != nullptr);
  REQUIRE(bp.new_page(&b) != nullptr);
  // Both frames pinned; no room for a third.
  page_id_t c;
  REQUIRE(bp.new_page(&c) == nullptr);

  // Unpin one and now it works.
  REQUIRE(bp.unpin_page(a, false));
  REQUIRE(bp.fetch_page(a) != nullptr);
  ::remove(path.c_str());
}
