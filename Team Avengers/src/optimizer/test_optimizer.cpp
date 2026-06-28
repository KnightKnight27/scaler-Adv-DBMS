// M4 test — cost-based optimizer decisions:
//   1. selectivity estimation (equality < range < none, AND multiplies)
//   2. access path: pk predicate -> IndexScan; non-pk predicate -> SeqScan
//   3. join order: smaller relation chosen as the inner (buffered) side
#include "optimizer.hpp"
#include "../database.hpp"
#include "../sql/parser.hpp"
#include <cassert>
#include <cstdio>

using namespace minidb;

int main() {
    // 1) selectivity ---------------------------------------------------------
    auto sel = [](const char* w) {
        auto e = Parser(std::string("SELECT * FROM t WHERE ") + w).parse().select.where;
        return Optimizer::selectivity(e.get());
    };
    assert(sel("id = 5") == 0.1);
    assert(sel("id > 5") == 0.33);
    assert(std::abs(sel("id = 5 AND age > 1") - 0.1 * 0.33) < 1e-9);   // AND multiplies
    assert(sel("id = 5 OR id = 6") > 0.1);                              // OR adds
    std::printf("[M4] selectivity estimates OK (eq=0.1, range=0.33, AND multiplies)\n");

    // 2 & 3) plan choices ----------------------------------------------------
    const std::string dbf = "minidb_opt_test.db";
    std::remove(dbf.c_str());
    DiskManager dm(dbf);
    BufferPoolManager bpm(64, &dm);
    Database db(&bpm);

    db.execute("CREATE TABLE big (id INT, v INT)");
    db.execute("CREATE TABLE small (id INT, big_id INT)");
    for (int i = 1; i <= 200; ++i) db.execute("INSERT INTO big VALUES (" + std::to_string(i) + ", " + std::to_string(i*2) + ")");
    for (int i = 1; i <= 10;  ++i) db.execute("INSERT INTO small VALUES (" + std::to_string(i) + ", " + std::to_string(i) + ")");

    Optimizer* opt = nullptr; (void)opt;
    Optimizer o(&db.catalog());

    // pk predicate -> IndexScan
    {
        std::string plan;
        o.plan(Parser("SELECT * FROM big WHERE id = 100").parse().select, &plan);
        assert(plan.find("IndexScan") != std::string::npos);
        std::printf("[M4] WHERE id=100  -> %s", plan.c_str());
    }
    // non-pk predicate -> SeqScan (index can't help)
    {
        std::string plan;
        o.plan(Parser("SELECT * FROM big WHERE v = 100").parse().select, &plan);
        assert(plan.find("SeqScan") != std::string::npos);
        std::printf("[M4] WHERE v=100   -> %s", plan.c_str());
    }
    // join: 'small' (10 rows) must be the inner side, 'big' (200) the outer
    {
        std::string plan;
        o.plan(Parser("SELECT * FROM big JOIN small ON big.id = small.big_id").parse().select, &plan);
        assert(plan.find("outer=big") != std::string::npos);
        assert(plan.find("inner=small") != std::string::npos);
        std::printf("[M4] join order: %.*s", (int)plan.find('\n')+1, plan.c_str());
    }

    std::remove(dbf.c_str());
    std::printf("[M4] cost-based optimizer: ALL CHECKS PASSED\n");
    return 0;
}
