// Verifies heap storage + catalog: create table, insert/scan/get/delete tuples
// across multiple pages, persist catalog metadata, reopen, and rebuild the
// primary-key index from the base table.
#include <cassert>
#include <cstdio>
#include <iostream>
#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"
#include "catalog/catalog.h"

using namespace minidb;

static Schema make_schema() {
    Schema s;
    s.columns = {{"id", TypeId::INTEGER}, {"name", TypeId::VARCHAR}, {"age", TypeId::INTEGER}};
    s.pk_index = 0;
    return s;
}

int main() {
    const char *db = "test_tbl.db";
    const char *cat = "test_tbl.catalog";
    std::remove(db); std::remove(cat);

    const int N = 2000; // enough rows to span many heap pages
    {
        DiskManager disk(db);
        BufferPoolManager bpm(32, &disk);
        Catalog catalog(&bpm);
        TableInfo *t = catalog.create_table("users", make_schema());
        assert(t->has_index);

        // Insert N rows and index each by primary key.
        for (int i = 0; i < N; ++i) {
            Tuple row = { Value(i), Value(std::string("user_") + std::to_string(i)), Value(20 + i % 50) };
            RID rid = t->heap->insert_tuple(row);
            assert(t->index->insert(i, rid));
        }

        // Index lookup -> heap fetch must return the right row.
        RID r;
        assert(t->index->search(1234, &r));
        Tuple got;
        assert(t->heap->get_tuple(r, &got));
        assert(got[0].as_int() == 1234);
        assert(got[1].as_string() == "user_1234");

        // Full scan returns all live rows.
        assert(t->heap->scan().size() == (size_t)N);

        // Delete a row; scan count drops and the slot reads as gone.
        assert(t->heap->delete_tuple(r));
        assert(!t->heap->get_tuple(r, &got));
        assert(t->heap->scan().size() == (size_t)N - 1);

        catalog.save(cat);
        bpm.flush_all();
        disk.sync();
    }

    // Reopen: load catalog metadata, rebuild indexes from the heap, re-query.
    {
        DiskManager disk(db);
        BufferPoolManager bpm(32, &disk);
        Catalog catalog(&bpm);
        catalog.load(cat);
        catalog.rebuild_indexes();
        TableInfo *t = catalog.get_table("users");
        assert(t != nullptr);
        // N-1 rows survived (one was deleted before save).
        assert(t->heap->scan().size() == (size_t)N - 1);
        RID r;
        assert(t->index->search(777, &r));
        Tuple got;
        assert(t->heap->get_tuple(r, &got) && got[1].as_string() == "user_777");
    }

    std::remove(db); std::remove(cat);
    std::cout << "[OK] heap + catalog: insert/scan/get/delete, persist, reopen, "
                 "rebuild index all verified" << std::endl;
    return 0;
}
