#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "minidb/buffer/buffer_pool.h"
#include "minidb/common/types.h"
#include "minidb/storage/disk_manager.h"
#include "minidb/storage/heap_table.h"
#include "minidb/storage/page_manager.h"

namespace {

using namespace minidb;

std::filesystem::path TempDb(const std::string& name) {
  auto path = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove(path);
  return path;
}

void Require(bool condition, const std::string& message) {
  if (!condition) {
    throw MiniDbError("test failed: " + message);
  }
}

void TestPageManagerAllocatesAndPersistsPages() {
  auto path = TempDb("minidb_m1_pages.db");
  PageId page_id = kInvalidPageId;

  {
    DiskManager disk(path);
    BufferPool buffer(disk, 2);
    PageManager pages(buffer);
    page_id = pages.AllocateHeapPage();
    Page& page = pages.FetchPage(page_id);
    auto slot = SlottedPage(page).Insert("hello");
    Require(slot.has_value(), "record should fit in fresh page");
    pages.Unpin(page_id, true);
    pages.FlushAll();
  }

  {
    DiskManager disk(path);
    BufferPool buffer(disk, 2);
    Page& page = buffer.FetchPage(page_id);
    auto record = SlottedPage(page).Get(0);
    Require(record.has_value() && *record == "hello", "page record should survive reopen");
    buffer.UnpinPage(page_id, false);
  }
}

void TestHeapTableInsertScanDelete() {
  auto path = TempDb("minidb_m1_heap.db");
  DiskManager disk(path);
  BufferPool buffer(disk, 3);
  PageId first_page = HeapTable::Create(buffer);
  HeapTable table(buffer, first_page);

  Rid alice = table.Insert(EncodeRow({"1", "Ada"}));
  table.Insert(EncodeRow({"2", "Grace"}));
  Require(table.Get(alice).has_value(), "inserted rid should be fetchable");
  Require(DecodeRow(*table.Get(alice))[1] == "Ada", "fetched row should decode");

  auto rows = table.Scan();
  Require(rows.size() == 2, "scan should return two live rows");
  Require(table.Delete(alice), "delete should mark row deleted");
  rows = table.Scan();
  Require(rows.size() == 1, "scan should skip deleted rows");
  Require(DecodeRow(rows[0].second)[1] == "Grace", "remaining row should be Grace");
}

void TestBufferPoolEvictsDirtyPages() {
  auto path = TempDb("minidb_m1_buffer.db");
  PageId first = kInvalidPageId;
  PageId second = kInvalidPageId;
  PageId third = kInvalidPageId;

  {
    DiskManager disk(path);
    BufferPool buffer(disk, 1);
    first = HeapTable::Create(buffer);
    HeapTable first_table(buffer, first);
    first_table.Insert("first");
    buffer.FlushAll();

    second = HeapTable::Create(buffer);
    HeapTable second_table(buffer, second);
    second_table.Insert("second");
    buffer.FlushAll();

    third = HeapTable::Create(buffer);
    HeapTable third_table(buffer, third);
    third_table.Insert("third");
    buffer.FlushAll();
  }

  {
    DiskManager disk(path);
    BufferPool buffer(disk, 1);
    Require(HeapTable(buffer, first).Scan()[0].second == "first", "first dirty page should persist");
    Require(HeapTable(buffer, second).Scan()[0].second == "second", "second dirty page should persist");
    Require(HeapTable(buffer, third).Scan()[0].second == "third", "third dirty page should persist");
  }
}

void TestHeapTableChainsAcrossPages() {
  auto path = TempDb("minidb_m1_heap_chain.db");
  std::vector<Rid> inserted;

  {
    DiskManager disk(path);
    BufferPool buffer(disk, 2);
    PageId first_page = HeapTable::Create(buffer);
    HeapTable table(buffer, first_page);

    for (int i = 0; i < 80; ++i) {
      inserted.push_back(table.Insert("row-" + std::to_string(i) + "-" + std::string(180, 'x')));
    }
    buffer.FlushAll();
    Require(disk.PageCount() > 1, "large heap should allocate additional pages");
  }

  {
    DiskManager disk(path);
    BufferPool buffer(disk, 2);
    HeapTable table(buffer, inserted.front().page_id);
    auto rows = table.Scan();
    Require(rows.size() == inserted.size(), "scan should traverse every heap page");
    Require(rows.front().second.rfind("row-0-", 0) == 0, "first chained row should persist");
    Require(rows.back().second.rfind("row-79-", 0) == 0, "last chained row should persist");
  }
}

void TestBufferPoolRejectsEvictionWhenAllFramesPinned() {
  auto path = TempDb("minidb_m1_pinned.db");
  DiskManager disk(path);
  BufferPool buffer(disk, 1);

  Page& page = buffer.NewPage();
  bool threw = false;
  try {
    buffer.NewPage();
  } catch (const MiniDbError&) {
    threw = true;
  }

  buffer.UnpinPage(page.page_id(), false);
  Require(threw, "buffer pool should reject eviction when every frame is pinned");
}

}  // namespace

int main() {
  try {
    TestPageManagerAllocatesAndPersistsPages();
    TestHeapTableInsertScanDelete();
    TestBufferPoolEvictsDirtyPages();
    TestHeapTableChainsAcrossPages();
    TestBufferPoolRejectsEvictionWhenAllFramesPinned();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "M1 tests passed\n";
  return 0;
}
