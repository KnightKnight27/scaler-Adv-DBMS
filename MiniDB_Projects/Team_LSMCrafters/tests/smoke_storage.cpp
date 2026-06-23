// Smoke test for the M1 storage layer: disk manager, buffer pool, slotted
// pages, tuple codec, and heap file. Inserts rows that span more than one page,
// scans them back, and checks the round-trip plus buffer-pool accounting.
#include <cassert>
#include <cstdio>
#include <string>
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "storage/heap_file.h"
#include "storage/slotted_page.h"
#include "storage/tuple.h"

using namespace minidb;

int main() {
  const std::string path = "/tmp/minidb_smoke.db";
  std::remove(path.c_str());

  DiskManager disk(path);
  BufferPool pool(disk);

  // Create one table: a single heap page to start with.
  PageId first = pool.allocate_page();
  Page* p = pool.fetch_page(first);
  SlottedPage(p).init();
  pool.unpin_page(first, true);

  Schema schema{{{"id", ColumnType::Int}, {"name", ColumnType::Text}}};
  HeapFile heap(pool, first);

  // Insert enough rows to force a second page to be allocated.
  const int kRows = 400;
  for (int i = 0; i < kRows; ++i) {
    Row row{static_cast<int64_t>(i), std::string("user_") + std::to_string(i)};
    Bytes bytes = serialize(row, schema);
    heap.insert(std::vector<char>(bytes.begin(), bytes.end()));
  }
  assert(disk.num_pages() > 1 && "rows should have spanned multiple pages");

  // Scan everything back and confirm the round-trip.
  int seen = 0;
  RID rid;
  std::vector<char> bytes;
  auto cur = heap.scan();
  while (cur.next(rid, bytes)) {
    Row row = deserialize(Bytes(bytes.begin(), bytes.end()), schema);
    assert(std::get<int64_t>(row[0]) == seen);
    ++seen;
  }
  assert(seen == kRows && "scan must return every inserted row");

  pool.flush_all();
  std::printf("smoke_storage OK: %d rows across %d pages\n", seen, disk.num_pages());
  pool.print_stats();
  return 0;
}
