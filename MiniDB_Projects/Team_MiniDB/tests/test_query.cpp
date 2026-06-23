// M3 tests: storage engine, Volcano operators, and the cost-based optimizer's
// plan choices (index vs seq scan, hash join build side, aggregation).
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "catalog/catalog.h"
#include "catalog/record.h"
#include "engine/rowstore_engine.h"
#include "execution/operators.h"
#include "parser/ast.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "planner/optimizer.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

using namespace minidb;

static int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

struct TestDB {
    DiskManager    disk;
    BufferPool     pool;
    Catalog        cat;
    RowStoreEngine engine;
    explicit TestDB(const std::string& p)
        : disk(p), pool(64, &disk), cat(&pool, p + ".cat"), engine(&cat, &pool, &disk) {}
};

static void put_row(RowStoreEngine& e, const std::string& tbl, const Schema& s,
                    std::int64_t key, const std::vector<Value>& vals) {
    e.put(tbl, key, Record::serialize(s, vals));
}

// Plan + run a SELECT, returning result tuples and the plan explanation.
static std::vector<Tuple> run_select(Catalog& cat, RowStoreEngine& eng,
                                     const std::string& sql, std::string& explain) {
    Lexer lex(sql);
    Parser parser(lex.tokenize());
    StmtPtr stmt = parser.parse();
    auto* sel = static_cast<SelectStmt*>(stmt.get());
    Optimizer opt(&cat, &eng);
    auto plan = opt.plan(*sel);
    explain = opt.explanation();
    std::vector<Tuple> rows;
    plan->open();
    Tuple t;
    while (plan->next(t)) rows.push_back(t);
    plan->close();
    return rows;
}

int main() {
    const std::string path = "test_query.db";
    std::remove(path.c_str());
    std::remove((path + ".cat").c_str());

    TestDB db(path);
    Schema cust({{"id", ValueType::INT, 8}, {"name", ValueType::VARCHAR, 20},
                 {"country", ValueType::VARCHAR, 20}});
    Schema ord({{"id", ValueType::INT, 8}, {"cust", ValueType::INT, 8},
                {"amount", ValueType::DOUBLE, 8}});
    db.engine.create_table("customers", cust, 0);
    db.engine.create_table("orders", ord, 0);

    put_row(db.engine, "customers", cust, 1, {std::int64_t{1}, std::string("alice"), std::string("IN")});
    put_row(db.engine, "customers", cust, 2, {std::int64_t{2}, std::string("bob"),   std::string("US")});
    put_row(db.engine, "customers", cust, 3, {std::int64_t{3}, std::string("carol"), std::string("IN")});
    put_row(db.engine, "customers", cust, 4, {std::int64_t{4}, std::string("dan"),   std::string("UK")});
    put_row(db.engine, "orders", ord, 10, {std::int64_t{10}, std::int64_t{1}, 100.0});
    put_row(db.engine, "orders", ord, 11, {std::int64_t{11}, std::int64_t{1}, 250.0});
    put_row(db.engine, "orders", ord, 12, {std::int64_t{12}, std::int64_t{2}, 80.0});
    put_row(db.engine, "orders", ord, 13, {std::int64_t{13}, std::int64_t{3}, 300.0});
    put_row(db.engine, "orders", ord, 14, {std::int64_t{14}, std::int64_t{3}, 50.0});
    put_row(db.engine, "orders", ord, 15, {std::int64_t{15}, std::int64_t{4}, 90.0});

    std::string ex;

    // selective equality on PK -> IndexScan
    auto r1 = run_select(db.cat, db.engine, "SELECT id, name FROM customers WHERE id = 3", ex);
    CHECK(ex.find("IndexScan") != std::string::npos);
    CHECK(r1.size() == 1 && std::get<std::int64_t>(r1[0].values[0]) == 3);

    // non-selective range -> SeqScan
    auto r2 = run_select(db.cat, db.engine, "SELECT id FROM customers WHERE id >= 2", ex);
    CHECK(ex.find("SeqScan") != std::string::npos);
    CHECK(r2.size() == 3);

    // join + filter; hash join builds the smaller table (customers, 4 < 6)
    auto r3 = run_select(db.cat, db.engine,
        "SELECT c.name, o.amount FROM customers c JOIN orders o ON c.id = o.cust WHERE o.amount > 100", ex);
    CHECK(ex.find("HashJoin(build on customers)") != std::string::npos);
    CHECK(r3.size() == 2);  // alice 250, carol 300

    // group by + aggregates over the join
    auto r4 = run_select(db.cat, db.engine,
        "SELECT c.country, COUNT(*), SUM(o.amount) FROM customers c JOIN orders o ON c.id = o.cust GROUP BY c.country", ex);
    CHECK(r4.size() == 3);
    for (auto& row : r4) {
        if (std::get<std::string>(row.values[0]) == "IN") {
            CHECK(std::get<std::int64_t>(row.values[1]) == 4);   // 4 IN orders
            CHECK(std::get<double>(row.values[2]) == 700.0);     // 100+250+300+50
        }
    }

    // delete + recount
    db.engine.erase("orders", 14);  // amount 50
    auto r5 = run_select(db.cat, db.engine, "SELECT COUNT(*) FROM orders", ex);
    CHECK(r5.size() == 1 && std::get<std::int64_t>(r5[0].values[0]) == 5);

    std::remove(path.c_str());
    std::remove((path + ".cat").c_str());
    if (g_failures == 0) { std::cout << "ALL QUERY TESTS PASSED\n"; return 0; }
    std::cerr << g_failures << " CHECK(s) FAILED\n";
    return 1;
}
