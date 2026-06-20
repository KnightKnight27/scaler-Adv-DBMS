// M3 test — execution operators. Builds plan trees by hand (the optimizer in
// M4 will build them automatically) and checks each operator's output:
//   1. SeqScan + WHERE filter
//   2. IndexScan over a key range (must equal the filtered seq scan)
//   3. NestedLoopJoin across two tables
//   4. Projection of a subset of columns
#include "operators.hpp"
#include "../sql/parser.hpp"
#include <cassert>
#include <cstdio>
#include <set>

using namespace minidb;

static void load(TableInfo* t, Tuple row) {
    RID rid = t->heap->insert(row.serialize(t->schema));
    t->index->insert(t->pk_of(row), rid);
}

int main() {
    const std::string dbf = "minidb_exec_test.db";
    std::remove(dbf.c_str());
    DiskManager dm(dbf);
    BufferPoolManager bpm(64, &dm);
    Catalog cat(&bpm);

    // users(id, name, age) and orders(oid, uid, amount)
    TableInfo* users  = cat.create_table("users",
        Schema{{{"id", ColType::INT}, {"name", ColType::TEXT}, {"age", ColType::INT}}});
    TableInfo* orders = cat.create_table("orders",
        Schema{{{"oid", ColType::INT}, {"uid", ColType::INT}, {"amount", ColType::INT}}});

    for (int i = 1; i <= 100; ++i)
        load(users, Tuple{{Value::Int(i), Value::Text("u" + std::to_string(i)), Value::Int(18 + i % 40)}});
    for (int i = 1; i <= 100; ++i)
        load(orders, Tuple{{Value::Int(i), Value::Int((i % 100) + 1), Value::Int(i * 10)}});

    // 1) SeqScan + WHERE id >= 90
    {
        auto where = Parser("SELECT * FROM users WHERE id >= 90").parse().select.where;
        SeqScanExecutor s(users, where.get());
        s.open();
        int count = 0; Tuple t;
        while (s.next(&t)) { assert(t.values[0].i >= 90); ++count; }
        assert(count == 11);     // ids 90..100
        std::printf("[M3] SeqScan + filter (id>=90) -> %d rows  OK\n", count);
    }

    // 2) IndexScan over [90, INT_MAX] must match the seq-scan result exactly
    {
        IndexScanExecutor s(users, 90, std::numeric_limits<int64_t>::max(), nullptr);
        s.open();
        std::set<int64_t> ids; Tuple t;
        while (s.next(&t)) ids.insert(t.values[0].i);
        assert(ids.size() == 11 && *ids.begin() == 90 && *ids.rbegin() == 100);
        std::printf("[M3] IndexScan range [90,max] -> %zu rows, same as SeqScan  OK\n", ids.size());
    }

    // 3) Join users.id = orders.uid, projecting users.name + orders.amount
    {
        auto left  = std::make_unique<SeqScanExecutor>(users, nullptr);
        auto right = std::make_unique<SeqScanExecutor>(orders, nullptr);
        auto join  = std::make_unique<NestedLoopJoinExecutor>(
            std::move(left), std::move(right), "users.id", "orders.uid");
        ProjectionExecutor proj(std::move(join), {"users.name", "orders.amount"});
        proj.open();
        int count = 0; Tuple t;
        while (proj.next(&t)) { assert(t.values.size() == 2); ++count; }
        assert(count == 100);    // each order matches exactly one user
        std::printf("[M3] Join users.id=orders.uid + projection -> %d rows  OK\n", count);
    }

    std::remove(dbf.c_str());
    std::printf("[M3] execution engine: ALL CHECKS PASSED\n");
    return 0;
}
