#include <cassert>
#include <cstdio>
#include <iostream>

#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "storage/heap_file.h"

using namespace minidb;

int main() {
  std::remove("test_storage.db");
  std::remove("test_storage.db.wal");
  {
    DiskManager dm("test_storage.db");
    BufferPool bpm(8, &dm);

    page_id_t pid;
    Page *p = bpm.NewPage(&pid);
    HeapPage hp(p->GetData());
    hp.Init();
    bpm.UnpinPage(pid, true);

    HeapFile heap(&bpm, pid);
    RID r1 = heap.InsertTuple("hello");
    RID r2 = heap.InsertTuple("world!!");
    assert(r1.Valid());
    assert(r2.Valid());

    std::string out;
    assert(heap.GetTuple(r1, &out) && out == "hello");
    assert(heap.GetTuple(r2, &out) && out == "world!!");

    assert(heap.DeleteTuple(r1));
    assert(!heap.GetTuple(r1, &out));

    bpm.FlushAllPages();
  }
  {
    // Reopen and verify durability across a process-level "restart".
    DiskManager dm("test_storage.db");
    BufferPool bpm(8, &dm);
    HeapFile heap(&bpm, 0);
    std::string out;
    assert(heap.GetTuple(RID{0, 1}, &out) && out == "world!!");
  }
  std::remove("test_storage.db");
  std::remove("test_storage.db.wal");
  std::cout << "storage_test PASSED\n";
  return 0;
}
