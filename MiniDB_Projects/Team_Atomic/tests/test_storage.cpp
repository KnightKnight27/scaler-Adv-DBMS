// Smoke test for the storage layer: disk manager + buffer pool + eviction.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include "storage/disk_manager.h"
#include "storage/buffer_pool_manager.h"

using namespace minidb;

int main() {
  const char* path = "test_storage.db";
  std::remove(path);

  DiskManager dm(path);
  // Tiny pool of 3 frames so we can force eviction.
  BufferPoolManager bpm(3, &dm);

  // Allocate 3 pages, write a signature into each.
  page_id_t ids[5];
  for (int i = 0; i < 3; i++) {
    Page* p = bpm.NewPage(&ids[i]);
    assert(p != nullptr);
    std::snprintf(p->Data(), PAGE_SIZE, "page-%d-content", ids[i]);
    bpm.UnpinPage(ids[i], /*dirty=*/true);
  }

  // Allocate 2 more -> forces eviction of the LRU pages (written back to disk).
  for (int i = 3; i < 5; i++) {
    Page* p = bpm.NewPage(&ids[i]);
    assert(p != nullptr);
    std::snprintf(p->Data(), PAGE_SIZE, "page-%d-content", ids[i]);
    bpm.UnpinPage(ids[i], true);
  }

  // Re-fetch an evicted page; its content must survive via disk.
  Page* p0 = bpm.FetchPage(ids[0]);
  assert(p0 != nullptr);
  char expected[64];
  std::snprintf(expected, sizeof(expected), "page-%d-content", ids[0]);
  assert(std::strcmp(p0->Data(), expected) == 0);
  bpm.UnpinPage(ids[0], false);

  // Pin all 3 frames; a 4th fetch of an uncached page must fail (no victim).
  Page* a = bpm.FetchPage(ids[1]);
  Page* b = bpm.FetchPage(ids[2]);
  Page* c = bpm.FetchPage(ids[3]);
  assert(a && b && c);
  Page* d = bpm.FetchPage(ids[4]);
  assert(d == nullptr);  // pool exhausted
  bpm.UnpinPage(ids[1], false);
  bpm.UnpinPage(ids[2], false);
  bpm.UnpinPage(ids[3], false);

  bpm.FlushAll();
  std::cout << "[storage] disk pages=" << dm.NumPages()
            << " writes=" << dm.GetNumWrites()
            << " reads=" << dm.GetNumReads() << "\n";
  std::cout << "[storage] OK: alloc, read/write, eviction, write-back all pass\n";
  std::remove(path);
  return 0;
}
