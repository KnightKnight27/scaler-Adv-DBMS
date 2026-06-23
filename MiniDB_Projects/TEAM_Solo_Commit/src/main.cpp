// MiniDB - interactive shell and demo driver.
//   ./minidb [db_file]        start a REPL on db_file (default: minidb.db)
//   ./minidb --demo           run the scripted end-to-end demonstration
//
// In the REPL, type SQL terminated by a newline. Prefix a query with EXPLAIN to see the plan
// the optimizer chose. Type \q to quit, \dt to list tables.
#include <atomic>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

#include "database.h"
#include "mvcc/version_store.h"

using namespace minidb;

namespace {

void PrintResult(const Result& r, bool show_plan) {
    if (!r.ok) { std::cout << "  error: " << r.error << "\n"; return; }
    if (show_plan && !r.explain.empty()) std::cout << "  plan: " << r.explain << "\n";
    if (r.is_query) {
        std::cout << "  ";
        for (size_t i = 0; i < r.schema.Count(); ++i)
            std::cout << (i ? " | " : "") << r.schema.GetColumn(i).name;
        std::cout << "\n  " << std::string(44, '-') << "\n";
        for (const auto& t : r.rows) {
            std::cout << "  ";
            for (size_t i = 0; i < t.Count(); ++i)
                std::cout << (i ? " | " : "") << t.GetValue(i).ToString();
            std::cout << "\n";
        }
        std::cout << "  (" << r.rows.size() << " row" << (r.rows.size() == 1 ? "" : "s") << ")\n";
    } else {
        std::cout << "  " << r.message << "\n";
    }
}

std::string FirstWordUpper(const std::string& s) {
    std::string r;
    for (char c : s) { if (c == ' ') break; r.push_back(static_cast<char>(std::toupper(c))); }
    return r;
}

void Repl(Database& db) {
    std::cout << "MiniDB shell. End statements with a newline; \\q to quit, \\dt to list tables.\n";
    std::string line;
    while (true) {
        std::cout << "minidb> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        if (line == "\\q" || line == "quit" || line == "exit") break;
        if (line == "\\dt") {
            for (const auto& name : db.catalog()->ListTables()) std::cout << "  " << name << "\n";
            continue;
        }
        bool explain = FirstWordUpper(line) == "EXPLAIN";
        std::string sql = explain ? line.substr(line.find(' ') + 1) : line;
        PrintResult(db.Execute(sql), explain);
    }
    db.Flush();
    std::cout << "bye\n";
}

void Banner(const std::string& title) {
    std::cout << "\n==================== " << title << " ====================\n";
}

void RunDemo() {
    const std::string path = "/tmp/minidb_demo.db";
    std::remove(path.c_str());
    std::remove((path + ".wal").c_str());

    Banner("1. Storage + SQL pipeline");
    {
        Database db(path, 64);
        std::cout << "> CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR, age INT)\n";
        PrintResult(db.Execute("CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR, age INT)"), false);
        std::cout << "> CREATE TABLE enroll (sid INT, course VARCHAR)\n";
        PrintResult(db.Execute("CREATE TABLE enroll (sid INT, course VARCHAR)"), false);
        for (int i = 1; i <= 6; ++i)
            db.Execute("INSERT INTO students VALUES (" + std::to_string(i) + ", 'student_" +
                       std::to_string(i) + "', " + std::to_string(18 + i) + ")");
        db.Execute("INSERT INTO enroll VALUES (1,'DB'),(1,'OS'),(3,'AI'),(5,'ML')");
        std::cout << "> SELECT * FROM students\n";
        PrintResult(db.Execute("SELECT * FROM students"), false);
        std::cout << "\n> SELECT id, name FROM students WHERE age >= 22 AND id != 5\n";
        PrintResult(db.Execute("SELECT id, name FROM students WHERE age >= 22 AND id != 5"), true);
        std::cout << "\n> SELECT * FROM students INNER JOIN enroll ON students.id = enroll.sid\n";
        PrintResult(db.Execute("SELECT * FROM students INNER JOIN enroll ON students.id = enroll.sid"), true);

        Banner("2. Cost-based optimizer: index scan vs sequential scan");
        std::cout << "> EXPLAIN SELECT * FROM students WHERE id = 3   (equality on PK -> index)\n";
        PrintResult(db.Execute("SELECT * FROM students WHERE id = 3"), true);
        std::cout << "> EXPLAIN SELECT * FROM students WHERE age = 20  (no index -> seq scan)\n";
        PrintResult(db.Execute("SELECT * FROM students WHERE age = 20"), true);
        db.Flush();
    }

    Banner("3. Transactions: Strict 2PL with deadlock detection");
    {
        Database db(path, 64);
        db.Execute("CREATE TABLE acct (id INT PRIMARY KEY, bal INT)");
        for (int i = 1; i <= 50; ++i)
            db.Execute("INSERT INTO acct VALUES (" + std::to_string(i) + ", " + std::to_string(i * 100) + ")");
        std::cout << "T1 locks row 1 then row 2; T2 locks row 2 then row 1 (opposite order)\n";
        std::atomic<int> commits{0}, aborts{0};
        auto worker = [&](int a, int b, const char* who) {
            Transaction* t = db.BeginTxn();
            if (!db.Execute("SELECT * FROM acct WHERE id=" + std::to_string(a) + " FOR UPDATE", t).ok) { ++aborts; return; }
            std::cout << "  " << who << " locked row " << a << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            Result r = db.Execute("SELECT * FROM acct WHERE id=" + std::to_string(b) + " FOR UPDATE", t);
            if (!r.ok) { std::cout << "  " << who << " aborted (deadlock victim)\n"; ++aborts; return; }
            db.CommitTxn(t);
            std::cout << "  " << who << " committed\n";
            ++commits;
        };
        std::thread t1(worker, 1, 2, "T1"), t2(worker, 2, 1, "T2");
        t1.join(); t2.join();
        std::cout << "result: " << commits.load() << " committed, " << aborts.load()
                  << " aborted (deadlock detected and broken)\n";
        db.Flush();
    }

    Banner("4. Crash recovery (Write-Ahead Log)");
    {
        Database db(path, 64);
        Transaction* t = db.BeginTxn();
        db.Execute("INSERT INTO acct VALUES (777, 7777)", t);
        db.CommitTxn(t);  // committed -> WAL flushed -> durable
        Transaction* u = db.BeginTxn();
        db.Execute("INSERT INTO acct VALUES (888, 8888)", u);  // never committed
        db.Flush();
        std::cout << "committed row 777, left row 888 in an uncommitted transaction, then crash\n";
        // db destructed here = crash before u commits
    }
    {
        Database db(path, 64);
        std::cout << "reopened: replaying the WAL...\n";
        std::cout << "> SELECT * FROM acct WHERE id = 777  (committed, should survive)\n";
        PrintResult(db.Execute("SELECT * FROM acct WHERE id = 777"), false);
        std::cout << "> SELECT * FROM acct WHERE id = 888  (uncommitted, should be gone)\n";
        PrintResult(db.Execute("SELECT * FROM acct WHERE id = 888"), false);
    }

    Banner("5. Extension Track B: MVCC snapshot isolation");
    {
        VersionStore vs;
        vs.Write(1, /*key=*/100, "balance=1000"); vs.Commit(1, 10);
        vs.Write(2, 100, "balance=2000"); vs.Commit(2, 20);
        std::string out;
        vs.ReadSnapshot(15, 100, &out);
        std::cout << "  reader with snapshot ts=15 sees: " << out << "  (the value committed at ts=10)\n";
        vs.ReadSnapshot(25, 100, &out);
        std::cout << "  reader with snapshot ts=25 sees: " << out << "  (the value committed at ts=20)\n";
        vs.Write(3, 100, "balance=9999");  // a concurrent writer, not yet committed
        out = "(none)";
        vs.ReadSnapshot(99, 100, &out);
        std::cout << "  reader with snapshot ts=99 sees: " << out
                  << "  (uncommitted writer is invisible: readers never block on writers)\n";
    }

    std::cout << "\nDemo complete. All six core features plus the MVCC extension were exercised.\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string arg1 = argc > 1 ? argv[1] : "";
    if (arg1 == "--demo") { RunDemo(); return 0; }
    std::string db_file = arg1.empty() ? "minidb.db" : arg1;
    Database db(db_file);
    Repl(db);
    return 0;
}
