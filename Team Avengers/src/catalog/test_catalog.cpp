// Bridge test (M2->M3): tuple serialization + heap pages + catalog + index
// working together. Inserts rows through the catalog, then checks: heap scan
// sees all live rows, the primary index resolves key->RID->tuple, and a delete
// removes the row from BOTH the heap and the index. Spans many pages to make
// sure the heap's page-chaining and the buffer pool cooperate.
#include "catalog.hpp"
#include <cassert>
#include <cstdio>
#include <set>

using namespace minidb;

int main() {
    const std::string dbf = "minidb_catalog_test.db";
    std::remove(dbf.c_str());
    DiskManager dm(dbf);
    BufferPoolManager bpm(32, &dm);
    Catalog cat(&bpm);

    // users(id INT pk, name TEXT, age INT)
    Schema schema{{{"id", ColType::INT}, {"name", ColType::TEXT}, {"age", ColType::INT}}};
    TableInfo* users = cat.create_table("users", schema);

    // insert 1000 rows: enough to span dozens of heap pages
    const int N = 1000;
    for (int i = 1; i <= N; ++i) {
        Tuple t{{Value::Int(i), Value::Text("user_" + std::to_string(i)), Value::Int(20 + i % 50)}};
        RID rid = users->heap->insert(t.serialize(schema));
        bool ok = users->index->insert(users->pk_of(t), rid);
        assert(ok && "primary key insert into index must succeed");
    }
    std::printf("[M3-bridge] inserted %d rows across the heap + index\n", N);

    // heap scan should see exactly N live tuples, all distinct ids
    {
        std::set<int64_t> seen;
        users->heap->scan([&](RID, const std::string& bytes) {
            Tuple t = Tuple::deserialize(bytes.data(), schema);
            seen.insert(t.values[0].i);
        });
        assert((int)seen.size() == N);
        std::printf("[M3-bridge] heap scan returned all %zu live rows\n", seen.size());
    }

    // index lookup: id=777 -> RID -> tuple, and the row's contents are right
    {
        RID rid;
        bool found = users->index->search(777, &rid);
        assert(found);
        std::string bytes;
        assert(users->heap->get(rid, &bytes));
        Tuple t = Tuple::deserialize(bytes.data(), schema);
        assert(t.values[0].i == 777 && t.values[1].s == "user_777");
        std::printf("[M3-bridge] index point lookup id=777 -> '%s'  OK\n",
                    t.values[1].s.c_str());
    }

    // delete id=500 from heap + index; it must vanish from both access paths
    {
        RID rid;
        assert(users->index->search(500, &rid));
        assert(users->heap->erase(rid));
        assert(users->index->erase(500));
        std::string bytes;
        assert(!users->heap->get(rid, &bytes) && "tombstoned slot must read as gone");
        assert(!users->index->search(500, &rid) && "index must no longer resolve it");
        std::printf("[M3-bridge] delete id=500 removed from heap AND index  OK\n");
    }

    std::remove(dbf.c_str());
    std::printf("[M3-bridge] record + catalog layer: ALL CHECKS PASSED\n");
    return 0;
}
