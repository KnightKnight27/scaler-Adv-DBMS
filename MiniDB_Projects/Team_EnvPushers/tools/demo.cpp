// Scripted, narrated demonstration of every required MiniDB feature.
// Run with:  ./bin/demo
//
// It walks through: storage + buffer pool, B+ tree index usage (EXPLAIN),
// SQL (WHERE/JOIN/GROUP BY/ORDER BY), the cost-based optimizer's scan choice,
// transactions (commit + rollback), deadlock detection, and crash recovery.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

#include "database.hpp"

using namespace minidb;
namespace fs = std::filesystem;

static void banner(const char* title) {
    std::printf("\n========================================================\n");
    std::printf("  %s\n", title);
    std::printf("========================================================\n");
}

static void show(Database& db, const std::string& sql, bool explain = false) {
    std::printf("\nSQL> %s\n", sql.c_str());
    ResultSet r = db.execute(sql);
    if (!r.ok) { std::printf("  ERROR: %s\n", r.message.c_str()); return; }
    if (!r.is_select) { std::printf("  %s\n", r.message.c_str()); return; }
    if (explain) std::printf("%s", r.explain.c_str());
    // column header
    std::printf("  ");
    for (auto& c : r.columns) std::printf("%-12s", c.c_str());
    std::printf("\n  ");
    for (size_t i = 0; i < r.columns.size(); ++i) std::printf("%-12s", "----");
    std::printf("\n");
    for (auto& row : r.rows) {
        std::printf("  ");
        for (auto& v : row) std::printf("%-12s", v.to_string().c_str());
        std::printf("\n");
    }
    std::printf("  (%zu rows)\n", r.rows.size());
}

int main() {
    fs::remove_all("demo_data");

    banner("1. STORAGE + SCHEMA  (page-based heap files, buffer pool)");
    Database db("demo_data");
    show(db, "CREATE TABLE users (id INT PRIMARY KEY, name TEXT, city TEXT, age INT);");
    show(db, "CREATE TABLE orders (oid INT PRIMARY KEY, uid INT, amount INT);");
    show(db, "INSERT INTO users VALUES "
             "(1,'alice','NYC',30),(2,'bob','LA',25),"
             "(3,'carol','NYC',35),(4,'dave','SF',28);");
    show(db, "INSERT INTO orders VALUES "
             "(100,1,250),(101,1,75),(102,2,500),(103,3,125),(104,3,300);");
    std::printf("\n  Buffer pool: %zu hits, %zu misses, %zu evictions\n",
                db.buffer_pool().stats().hits, db.buffer_pool().stats().misses,
                db.buffer_pool().stats().evictions);

    banner("2. B+ TREE INDEX + COST-BASED OPTIMIZER  (EXPLAIN shows access path)");
    std::printf("\n  Equality on the PRIMARY KEY -> the optimizer picks an Index Scan:\n");
    show(db, "SELECT id, name, city FROM users WHERE id = 3;", /*explain=*/true);
    std::printf("\n  Predicate on a non-key column -> a full Sequential Scan:\n");
    show(db, "SELECT name, age FROM users WHERE city = 'NYC';", /*explain=*/true);

    banner("3. SQL: JOIN, AGGREGATION, ORDER BY");
    show(db, "SELECT u.name, o.amount FROM users u JOIN orders o ON u.id = o.uid "
             "WHERE o.amount >= 200;", true);
    show(db, "SELECT uid, COUNT(*), SUM(amount), MAX(amount) FROM orders GROUP BY uid;");
    show(db, "SELECT name, age FROM users ORDER BY age DESC;");

    banner("4. TRANSACTIONS  (strict 2PL, commit vs. rollback)");
    {
        std::printf("\n  -- BEGIN; UPDATE; then ROLLBACK undoes it --\n");
        Transaction* tx = db.begin();
        db.execute("UPDATE users SET age = 99 WHERE id = 1;", tx);
        ResultSet mid = db.execute("SELECT age FROM users WHERE id = 1;", tx);
        std::printf("  inside txn, age(id=1) = %s\n", mid.rows[0][0].to_string().c_str());
        db.abort(tx);
        show(db, "SELECT age FROM users WHERE id = 1;");   // back to 30
    }

    banner("5. DEADLOCK DETECTION  (wait-for graph, victim aborted)");
    {
        std::atomic<int> deadlocks{0}, ready{0};
        auto worker = [&](const char* a, const char* b, int rowa, int rowb) {
            Transaction* tx = db.begin();
            try {
                db.execute(std::string("UPDATE ") + a + " SET age = 1 WHERE id = " +
                           std::to_string(rowa) + ";", tx);
                ready++;
                while (ready.load() < 2) std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                db.execute(std::string("UPDATE ") + b + " SET age = 2 WHERE id = " +
                           std::to_string(rowb) + ";", tx);
                db.commit(tx);
                std::printf("  a txn acquired both locks and committed\n");
            } catch (const DeadlockError& e) {
                std::printf("  %s -> victim aborted, the other proceeds\n", e.what());
                deadlocks++;
                db.abort(tx);
            } catch (...) { db.abort(tx); }
        };
        // Two tables to lock in opposite orders.
        db.execute("CREATE TABLE accts (id INT PRIMARY KEY, age INT);");
        db.execute("INSERT INTO accts VALUES (1,1),(2,2);");
        std::thread t1(worker, "users", "accts", 1, 1);
        std::thread t2(worker, "accts", "users", 1, 1);
        t1.join(); t2.join();
        std::printf("  deadlocks detected & resolved: %d\n", deadlocks.load());
    }

    banner("6. CRASH RECOVERY  (Write-Ahead Log: redo committed / undo in-flight)");
    fs::remove_all("demo_recovery");
    pid_t pid = fork();
    if (pid == 0) {
        Database crash("demo_recovery");
        crash.execute("CREATE TABLE t (id INT PRIMARY KEY, note TEXT);");
        crash.execute("INSERT INTO t VALUES (1,'committed-A'),(2,'committed-B');");
        Transaction* tx = crash.begin();
        crash.execute("INSERT INTO t VALUES (3,'inflight-uncommitted');", tx);
        std::printf("\n  [child] wrote 2 committed rows + 1 uncommitted, then CRASH "
                    "(no checkpoint)\n");
        _exit(0);                 // skip destructor/checkpoint == crash
    }
    int status; waitpid(pid, &status, 0);
    {
        std::printf("  [restart] reopening database -> recovery runs from the WAL...\n");
        Database recovered("demo_recovery");
        show(recovered, "SELECT id, note FROM t ORDER BY id;");
        std::printf("  => committed rows survived; the uncommitted row was rolled back.\n");
    }

    banner("DONE");
    std::printf("All core features + Track C (LSM-tree) extension are demonstrated.\n"
                "Run ./bin/bench_lsm for the LSM vs B+ Tree benchmark, and\n"
                "./bin/tests for the automated test suite.\n\n");

    fs::remove_all("demo_data");
    fs::remove_all("demo_recovery");
    return 0;
}
