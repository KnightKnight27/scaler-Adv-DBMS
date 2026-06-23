// Concurrency: two transactions lock two rows in opposite order (SELECT ... FOR UPDATE).
// Exactly one must be aborted as the deadlock victim; the other commits.
#include <atomic>
#include <thread>

#include "database.h"
#include "test_util.h"

using namespace minidb;

int main() {
    std::remove("/tmp/minidb_test_dl.db");
    std::remove("/tmp/minidb_test_dl.db.wal");
    Database db("/tmp/minidb_test_dl.db", 32);
    db.Execute("CREATE TABLE acct (id INT PRIMARY KEY, bal INT)");
    // Enough rows that "WHERE id = k FOR UPDATE" uses the index and locks only that one row
    // (on a tiny table the optimizer would seq-scan and lock every row, defeating the setup).
    for (int i = 1; i <= 100; ++i)
        db.Execute("INSERT INTO acct VALUES (" + std::to_string(i) + ", " + std::to_string(i * 100) + ")");

    std::atomic<int> aborts{0}, commits{0};
    auto worker = [&](int first, int second) {
        Transaction* t = db.BeginTxn();
        Result r1 = db.Execute("SELECT * FROM acct WHERE id=" + std::to_string(first) + " FOR UPDATE", t);
        if (!r1.ok) { ++aborts; return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        Result r2 = db.Execute("SELECT * FROM acct WHERE id=" + std::to_string(second) + " FOR UPDATE", t);
        if (!r2.ok) { ++aborts; return; }
        db.CommitTxn(t);
        ++commits;
    };

    std::thread a(worker, 1, 2), b(worker, 2, 1);
    a.join();
    b.join();

    printf("  (commits=%d aborts=%d)\n", commits.load(), aborts.load());
    CHECK(aborts.load() == 1);   // exactly one victim
    CHECK(commits.load() == 1);  // the other made progress
    return minidb_test::Done("deadlock");
}
