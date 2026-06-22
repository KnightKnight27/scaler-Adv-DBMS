#include "catch.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "common/rid.h"
#include "storage/disk_storage_manager.h"
#include "storage/heap_table.h"

using namespace axiomdb;

namespace {
std::string temp_db_path(const char* tag) {
  return std::string("/tmp/axiomdb_test_") + tag + "_" + std::to_string(::getpid()) + ".wdb";
}
}  // namespace

TEST_CASE("HeapTable insert/get across many pages and scan", "[heap][m1]") {
  std::string path = temp_db_path("heap_scan");
  ::remove(path.c_str());
  DiskStorageManager dm(path);
  BufferPoolManager bp(&dm, /*pool_size=*/4, /*k=*/2);  // small pool forces real paging

  auto heap = HeapTable::create(&bp);

  // Insert enough ~200B records to span many pages (4KB/200 ~ 20 per page).
  const int N = 500;
  std::vector<RID> rids;
  for (int i = 0; i < N; ++i) {
    std::string rec = "row-" + std::to_string(i) + std::string(180, '.');
    rids.push_back(heap->insert(rec));
  }

  // Point lookups by RID.
  REQUIRE(heap->get(rids[0]).value().rfind("row-0", 0) == 0);
  REQUIRE(heap->get(rids[N - 1]).value().rfind("row-499", 0) == 0);

  // Full scan visits exactly the N live records.
  int count = 0;
  std::set<std::string> seen;
  for (auto cur = heap->scan(); cur.valid(); cur.next()) {
    seen.insert(std::string(cur.value()));
    ++count;
  }
  REQUIRE(count == N);
  REQUIRE(seen.size() == static_cast<size_t>(N));
}

TEST_CASE("HeapTable delete tombstones and scan skips them", "[heap][m1]") {
  std::string path = temp_db_path("heap_del");
  ::remove(path.c_str());
  DiskStorageManager dm(path);
  BufferPoolManager bp(&dm, 8, 2);
  auto heap = HeapTable::create(&bp);

  std::vector<RID> rids;
  for (int i = 0; i < 50; ++i) rids.push_back(heap->insert("val-" + std::to_string(i)));

  // Delete every even row.
  for (int i = 0; i < 50; i += 2) REQUIRE(heap->erase(rids[i]));
  REQUIRE_FALSE(heap->get(rids[0]).has_value());
  REQUIRE(heap->get(rids[1]).value() == "val-1");

  int count = 0;
  for (auto cur = heap->scan(); cur.valid(); cur.next()) ++count;
  REQUIRE(count == 25);  // odd rows survive
}

TEST_CASE("HeapTable persists across reopen via first_page_id", "[heap][m1]") {
  std::string path = temp_db_path("heap_persist");
  ::remove(path.c_str());

  page_id_t head;
  {
    DiskStorageManager dm(path);
    BufferPoolManager bp(&dm, 4, 2);
    auto heap = HeapTable::create(&bp);
    head = heap->first_page_id();
    for (int i = 0; i < 300; ++i) heap->insert("persist-" + std::to_string(i));
    bp.flush_all();
    dm.sync();
  }
  {
    DiskStorageManager dm(path);
    BufferPoolManager bp(&dm, 4, 2);
    HeapTable heap(&bp, head);  // reopen from persisted chain head
    int count = 0;
    bool saw_first = false, saw_last = false;
    for (auto cur = heap.scan(); cur.valid(); cur.next()) {
      if (cur.value() == "persist-0") saw_first = true;
      if (cur.value() == "persist-299") saw_last = true;
      ++count;
    }
    REQUIRE(count == 300);
    REQUIRE(saw_first);
    REQUIRE(saw_last);

    // The heap is still writable after reopen (tail hint was rebuilt).
    RID r = heap.insert("after-reopen");
    REQUIRE(heap.get(r).value() == "after-reopen");
  }
  ::remove(path.c_str());
}
