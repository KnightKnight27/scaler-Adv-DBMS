// Cost-based optimizer: an equality on an indexed column should pick an index scan once the
// table is large enough; a predicate on a non-indexed column falls back to a sequential scan.
#include "database.h"
#include "test_util.h"

using namespace minidb;

static bool contains(const std::string& s, const std::string& sub) {
    return s.find(sub) != std::string::npos;
}

int main() {
    std::remove("/tmp/minidb_test_opt.db");
    std::remove("/tmp/minidb_test_opt.db.wal");
    Database db("/tmp/minidb_test_opt.db", 64);
    db.Execute("CREATE TABLE t (id INT PRIMARY KEY, payload INT)");
    for (int i = 0; i < 500; ++i)
        db.Execute("INSERT INTO t VALUES (" + std::to_string(i) + ", " + std::to_string(i) + ")");

    // PK equality on a 500-row table: index scan wins.
    Result a = db.Execute("SELECT * FROM t WHERE id = 250");
    CHECK(contains(a.explain, "IndexScan"));
    CHECK(a.rows.size() == 1);

    // Predicate on the non-indexed column: sequential scan.
    Result b = db.Execute("SELECT * FROM t WHERE payload = 250");
    CHECK(contains(b.explain, "SeqScan"));
    CHECK(b.rows.size() == 1);

    // Join plan reports nested-loop with the chosen drive order.
    db.Execute("CREATE TABLE u (uid INT, t_id INT)");
    db.Execute("INSERT INTO u VALUES (1, 10), (2, 20)");
    Result j = db.Execute("SELECT * FROM t INNER JOIN u ON t.id = u.t_id");
    CHECK(contains(j.explain, "NestedLoopJoin"));

    return minidb_test::Done("optimizer");
}
