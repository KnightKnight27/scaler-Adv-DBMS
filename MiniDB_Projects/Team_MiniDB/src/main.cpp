#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "catalog/catalog.h"
#include "common/config.h"
#include "common/exception.h"
#include "engine/rowstore_engine.h"
#include "execution/executor.h"
#include "parser/ast.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "storage/heap_file.h"
#include "txn/lock_manager.h"
#include "txn/transaction.h"
#include "txn/txn_manager.h"

using namespace minidb;
using namespace std::chrono_literals;

// Bundles the full engine stack for a database file: disk -> buffer pool ->
// catalog -> row-store engine -> executor. Member order is the init order.
struct Database {
    DiskManager    disk;
    BufferPool     pool;
    Catalog        cat;
    RowStoreEngine engine;
    Executor       exec;
    explicit Database(const std::string& path)
        : disk(path),
          pool(DEFAULT_BUFFER_POOL_FRAMES, &disk),
          cat(&pool, path + ".cat"),
          engine(&cat, &pool, &disk),
          exec(&cat, &engine) {}
    void flush() { engine.flush(); }
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
    d.flush();
    return 0;
}

static int exec_sql(const std::string& db, const std::string& sql) {
    Database d(db);
    d.exec.execute_script(sql);
    d.flush();
    return 0;
}

static int repl(const std::string& db) {
    Database d(db);
    std::cout << "MiniDB REPL on " << db << " (one statement per line; Ctrl-D to quit)\n";
    std::string line;
    while (std::cout << "minidb> " && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        try { d.exec.execute_script(line); d.flush(); }
        catch (const std::exception& e) { std::cerr << "error: " << e.what() << "\n"; }
    }
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
        std::cout << "MiniDB\n"
                  << "usage:\n"
                  << "  minidb run <db> <file.sql>   execute a SQL script\n"
                  << "  minidb exec <db> \"<sql>\"      execute SQL passed on the command line\n"
                  << "  minidb repl <db>             interactive SQL shell\n"
                  << "  minidb parse \"<sql>\"          parse one statement (AST summary)\n"
                  << "  minidb concurrency           2PL concurrency + deadlock demo\n"
                  << "  minidb selftest [dbfile]     storage-layer self test\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
