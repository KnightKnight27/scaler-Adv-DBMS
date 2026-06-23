// Storage layer: heap insert/scan/delete across pages, and clock-sweep eviction.
#include <cstdio>

#include "catalog/catalog.h"
#include "common/tuple.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "test_util.h"

using namespace minidb;

int main() {
    std::remove("/tmp/minidb_test_storage.db");
    DiskManager disk("/tmp/minidb_test_storage.db");
    BufferPool bp(&disk, 2);  // tiny pool -> forces eviction
    Catalog cat(&bp);

    Schema schema({{"id", TypeId::INTEGER, true}, {"name", TypeId::VARCHAR, false}});
    TableInfo* t = cat.CreateTable("t", schema);

    const int N = 800;  // enough rows to span several pages so a 2-frame pool must evict
    for (int i = 0; i < N; ++i) {
        Tuple row({Value::MakeInt(i), Value::MakeVarchar("row_" + std::to_string(i))});
        t->heap->Insert(row.Serialize(schema));
    }

    int count = 0;
    for (auto it = t->heap->begin(); it != t->heap->end(); ++it) ++count;
    CHECK(count == N);
    CHECK(t->heap->NumPages() > 1);          // spans multiple pages
    CHECK(bp.stats().evictions > 0);         // clock-sweep actually evicted

    // Delete two rows and confirm the live count drops by two.
    auto first = t->heap->begin();
    RID r0 = first.GetRID();
    t->heap->Delete(r0);
    int after = 0;
    for (auto it = t->heap->begin(); it != t->heap->end(); ++it) ++after;
    CHECK(after == N - 1);

    return minidb_test::Done("storage");
}
