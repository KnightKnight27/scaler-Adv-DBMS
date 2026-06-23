#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "catalog/catalog.h"
#include "catalog/record.h"
#include "common/config.h"
#include "common/exception.h"
#include "engine/rowstore_engine.h"
#include "execution/executor.h"
#include "parser/ast.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "recovery/log_manager.h"
#include "recovery/recovery_manager.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "storage/heap_file.h"
#include "txn/lock_manager.h"
#include "txn/transaction.h"
#include "txn/txn_manager.h"

using namespace minidb;
using namespace std::chrono_literals;

// Bundles the full engine stack for a database file: disk -> buffer pool ->
// catalog -> row-store engine -> WAL -> executor. On open it runs crash recovery
// (replays the committed WAL); checkpoint() makes the data file durable and
// clears the WAL.
struct Database {
    DiskManager    disk;
    BufferPool     pool;
    Catalog        cat;
    RowStoreEngine engine;
    LogManager     log;
    Executor       exec;
    explicit Database(const std::string& path)
        : disk(path),
          pool(DEFAULT_BUFFER_POOL_FRAMES, &disk),
          cat(&pool, path + ".cat"),
          engine(&cat, &pool, &disk),
          log(path + ".wal"),
          exec(&cat, &engine, &log) {
        RecoveryManager::recover(&engine, log);  // replay committed WAL after a crash
    }
    void checkpoint() { engine.flush(); log.truncate(); }  // durable data + clear WAL
};

// M1 demo: storage stack + buffer-pool stats.
static int storage_selftest(const std::string& path) {
    std::remove(path.c_str());
    DiskManager disk(path);
    BufferPool  pool(8, &disk);
    PageId first = HeapFile::create(&pool);
    HeapFile heap(&pool, first);
    const int N = 2000;
    for (int i = 0; i < N; ++i) heap.insert("row-" + std::to_string(i) + "-payload-data");
    pool.flush_all();
    int count = 0; RID rid; std::string val;
    for (auto it = heap.begin(); it.next(rid, val); ) ++count;
    std::cout << "inserted=" << N << " scanned=" << count << "\n"
              << "db pages=" << disk.num_pages() << "\n"
              << "buffer pool: hits=" << pool.hits() << " misses=" << pool.misses()
              << " evictions=" << pool.evictions() << "\n";
    return count == N ? 0 : 1;
}

// M2 demo: tokenize + parse a statement, print an AST summary.
static int parse_demo(const std::string& sql) {
    Lexer lex(sql);
    Parser parser(lex.tokenize());
    StmtPtr stmt = parser.parse();
    switch (stmt->kind()) {
        case StmtKind::CreateTable: std::cout << "parsed: CREATE TABLE\n"; break;
        case StmtKind::Insert:      std::cout << "parsed: INSERT\n"; break;
        case StmtKind::Delete:      std::cout << "parsed: DELETE\n"; break;
        case StmtKind::Select:      std::cout << "parsed: SELECT\n"; break;
    }
    return 0;
}

// M4 demo: narrated 2PL scenarios over the lock manager with two threads.
static std::mutex g_log_mu;
static void say(const std::string& s) {
    std::lock_guard<std::mutex> lk(g_log_mu);
    std::cout << s << "\n";
}

static int concurrency_demo() {
    // Scenario 1: shared locks coexist.
    {
        LockManager lm;
        say("[scenario 1] two transactions read the same row (SHARED locks)");
        lm.acquire(1, "users:7", LockMode::SHARED); say("  T1 acquired SHARED users:7");
        lm.acquire(2, "users:7", LockMode::SHARED); say("  T2 acquired SHARED users:7 (compatible)");
        lm.release_all(1); lm.release_all(2);
    }
    // Scenario 2: a writer blocks a reader until commit.
    {
        LockManager lm;
        say("[scenario 2] a writer blocks a reader until it commits");
        lm.acquire(1, "users:7", LockMode::EXCLUSIVE); say("  T1 acquired EXCLUSIVE users:7");
        std::thread t2([&] {
            say("  T2 requests SHARED users:7 ... (blocks)");
            lm.acquire(2, "users:7", LockMode::SHARED);
            say("  T2 acquired SHARED users:7 (after T1 committed)");
            lm.release_all(2);
        });
        std::this_thread::sleep_for(150ms);
        say("  T1 commits, releasing its lock");
        lm.release_all(1);
        t2.join();
    }
    // Scenario 3: an induced deadlock; the detector aborts a victim.
    {
        LockManager lm;
        TransactionManager tm(&lm);
        Transaction t1 = tm.begin(), t2 = tm.begin();
        say("[scenario 3] induced deadlock (T" + std::to_string(t1.id) + " holds A wants B; T" +
            std::to_string(t2.id) + " holds B wants A)");
        std::mutex bm; std::condition_variable bcv; int arrived = 0;
        auto barrier = [&] {
            std::unique_lock<std::mutex> lk(bm);
            if (++arrived == 2) bcv.notify_all(); else bcv.wait(lk, [&] { return arrived == 2; });
        };
        auto work = [&](Transaction& self, const char* a, const char* b) {
            try {
                lm.acquire(self.id, a, LockMode::EXCLUSIVE);
                say("  T" + std::to_string(self.id) + " acquired EXCLUSIVE " + a);
                barrier();
                lm.acquire(self.id, b, LockMode::EXCLUSIVE);
                say("  T" + std::to_string(self.id) + " acquired EXCLUSIVE " + b + " -> COMMIT");
                tm.commit(self);
            } catch (const DeadlockException& e) {
                say(std::string("  ") + e.what() + " -> ABORT (releases locks)");
                tm.abort(self);
            }
        };
        std::thread a(work, std::ref(t1), "A", "B");
        std::thread b(work, std::ref(t2), "B", "A");
        a.join(); b.join();
    }
    return 0;
}

static std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw DBException("cannot open SQL file: " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static int run_file(const std::string& db, const std::string& file) {
    Database d(db);
    d.exec.execute_script(read_file(file));
    d.checkpoint();
    return 0;
}

static int exec_sql(const std::string& db, const std::string& sql) {
    Database d(db);
    d.exec.execute_script(sql);
    d.checkpoint();
    return 0;
}

static int repl(const std::string& db) {
    Database d(db);
    std::cout << "MiniDB REPL on " << db << " (one statement per line; Ctrl-D to quit)\n";
    std::string line;
    while (std::cout << "minidb> " && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        try { d.exec.execute_script(line); d.checkpoint(); }
        catch (const std::exception& e) { std::cerr << "error: " << e.what() << "\n"; }
    }
    return 0;
}

// Run SQL, then exit WITHOUT checkpointing -> committed work is durable in the
// WAL but the data file is not flushed. Simulates a crash; the next open
// recovers. Uses _Exit to skip all destructors (faithful to kill -9).
static int crash_run(const std::string& db, const std::string& sql) {
    Database d(db);
    d.exec.execute_script(sql);
    std::cout << "(committed to WAL; data NOT checkpointed) -- simulating crash\n";
    std::cout.flush();
    std::_Exit(0);
}

// Open a database (which recovers from the WAL), report its tables, checkpoint.
static int recover_open(const std::string& db) {
    Database d(db);
    std::cout << "recovered database " << db << "\n";
    for (const auto& name : d.cat.table_names()) {
        EngineStats st = d.engine.stats(name);
        std::cout << "  table " << name << ": " << st.live_rows << " row(s)\n";
    }
    d.checkpoint();
    return 0;
}

// Self-contained recovery demo: commit some rows + leave one uncommitted, crash
// (no checkpoint), reopen, and show committed survived while uncommitted rolled back.
static int recover_demo() {
    const std::string db = "minidb_recover_demo.db";
    std::remove(db.c_str()); std::remove((db + ".cat").c_str()); std::remove((db + ".wal").c_str());
    std::cout << "[recovery demo]\n";
    {
        Database d(db);
        d.exec.execute_script("CREATE TABLE accounts (id INT PRIMARY KEY, balance INT);");
        std::cout << "commit INSERT (1,100),(2,200)  [WAL flushed; data NOT checkpointed]\n";
        d.exec.execute_script("INSERT INTO accounts VALUES (1,100),(2,200);");
        // An uncommitted write: log a PUT with no matching COMMIT, also apply in memory.
        TableInfo* t = d.cat.get_table("accounts");
        std::string bytes = Record::serialize(t->schema, {std::int64_t{3}, std::int64_t{300}});
        d.engine.put("accounts", 3, bytes);
        d.log.append(LogRecord{LogType::PUT, 999, "accounts", 3, bytes});
        d.log.flush();
        std::cout << "wrote UNCOMMITTED row id=3 (PUT logged, no COMMIT)\n";
        std::cout << "*** crash (no checkpoint) ***\n";
    }
    {
        Database d(db);  // constructor runs recovery
        std::cout << "after recovery (committed rows survive, uncommitted rolled back):\n";
        d.exec.execute_script("SELECT id, balance FROM accounts;");
        d.checkpoint();
    }
    std::remove(db.c_str()); std::remove((db + ".cat").c_str()); std::remove((db + ".wal").c_str());
    return 0;
}

int main(int argc, char** argv) {
    std::string cmd = argc > 1 ? argv[1] : "";
    try {
        if (cmd == "selftest") return storage_selftest(argc > 2 ? argv[2] : "minidb_selftest.db");
        if (cmd == "parse") {
            if (argc < 3) { std::cerr << "usage: minidb parse \"<sql>\"\n"; return 2; }
            return parse_demo(argv[2]);
        }
        if (cmd == "run") {
            if (argc < 4) { std::cerr << "usage: minidb run <db> <file.sql>\n"; return 2; }
            return run_file(argv[2], argv[3]);
        }
        if (cmd == "exec") {
            if (argc < 4) { std::cerr << "usage: minidb exec <db> \"<sql>\"\n"; return 2; }
            return exec_sql(argv[2], argv[3]);
        }
        if (cmd == "repl") {
            if (argc < 3) { std::cerr << "usage: minidb repl <db>\n"; return 2; }
            return repl(argv[2]);
        }
        if (cmd == "concurrency") return concurrency_demo();
        if (cmd == "crash") {
            if (argc < 4) { std::cerr << "usage: minidb crash <db> \"<sql>\"\n"; return 2; }
            return crash_run(argv[2], argv[3]);
        }
        if (cmd == "recover") {
            if (argc < 3) { std::cerr << "usage: minidb recover <db>\n"; return 2; }
            return recover_open(argv[2]);
        }
        if (cmd == "recover-demo") return recover_demo();
        std::cout << "MiniDB\n"
                  << "usage:\n"
                  << "  minidb run <db> <file.sql>   execute a SQL script\n"
                  << "  minidb exec <db> \"<sql>\"      execute SQL passed on the command line\n"
                  << "  minidb repl <db>             interactive SQL shell\n"
                  << "  minidb parse \"<sql>\"          parse one statement (AST summary)\n"
                  << "  minidb concurrency           2PL concurrency + deadlock demo\n"
                  << "  minidb crash <db> \"<sql>\"     run SQL then crash (no checkpoint)\n"
                  << "  minidb recover <db>          open + recover from WAL, report tables\n"
                  << "  minidb recover-demo          self-contained crash/recovery demo\n"
                  << "  minidb selftest [dbfile]     storage-layer self test\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
