// ---------------------------------------------------------------------------
// main.cpp - MiniDB entry point.
//
//   ./minidb              -> interactive SQL REPL (persists to ./minidb_data.*)
//   ./minidb <prefix>     -> REPL persisting to <prefix>.db/.wal/.catalog
//   ./minidb demo         -> scripted walkthrough of every required feature,
//                            including a real crash + WAL recovery cycle.
//
// The REPL supports: CREATE TABLE, INSERT, SELECT (WHERE / JOIN), DELETE,
// EXPLAIN, BEGIN/COMMIT/ABORT, plus dot-commands (.tables, .mvcc, .exit).
// ---------------------------------------------------------------------------
#include "src/database.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstdio>

using namespace minidb;

static void print_rs(const ResultSet& rs) {
    if (!rs.plan.empty()) std::cout << rs.plan;
    if (rs.columns.empty()) return;
    for (size_t i = 0; i < rs.columns.size(); ++i)
        std::cout << (i ? " | " : "") << rs.columns[i];
    std::cout << "\n";
    std::string sep;
    for (size_t i = 0; i < rs.columns.size(); ++i) sep += (i ? "-+-" : "") + std::string(rs.columns[i].size(), '-');
    std::cout << sep << "\n";
    for (auto& row : rs.rows) {
        for (size_t i = 0; i < row.size(); ++i)
            std::cout << (i ? " | " : "") << value_to_string(row[i]);
        std::cout << "\n";
    }
    std::cout << "(" << rs.rows.size() << " row" << (rs.rows.size() == 1 ? "" : "s") << ")\n";
}

static std::string upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
static std::string first_word(const std::string& s) {
    size_t i = 0; while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
    size_t j = i; while (j < s.size() && !std::isspace((unsigned char)s[j])) j++;
    return s.substr(i, j - i);
}
static std::string strip(std::string s) {
    while (!s.empty() && (s.back() == ';' || std::isspace((unsigned char)s.back()))) s.pop_back();
    size_t i = 0; while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
    return s.substr(i);
}

static int run_repl(const std::string& prefix) {
    Database db(prefix, /*mvcc=*/true);
    std::cout << "MiniDB - type SQL ending in ';' (or .exit to quit, .help for help)\n";
    TxId session = INVALID_TX; // 0 == autocommit mode
    std::string line;
    while (true) {
        std::cout << (session ? "minidb(tx)> " : "minidb> ") << std::flush;
        if (!std::getline(std::cin, line)) break;
        std::string stmt = strip(line);
        if (stmt.empty()) continue;

        std::string kw = upper(first_word(stmt));
        try {
            if (stmt[0] == '.') {
                if (stmt == ".exit" || stmt == ".quit") break;
                else if (stmt == ".tables") {
                    for (auto& n : db.catalog()->table_names()) std::cout << "  " << n << "\n";
                } else if (stmt == ".mvcc on")  { db.set_mvcc(true);  std::cout << "MVCC reads ON\n"; }
                else if (stmt == ".mvcc off")   { db.set_mvcc(false); std::cout << "2PL (locking) reads ON\n"; }
                else if (stmt == ".help") {
                    std::cout << "  SQL: CREATE TABLE, INSERT, SELECT (WHERE/JOIN), DELETE, EXPLAIN\n"
                                 "  Tx : BEGIN / COMMIT / ABORT\n"
                                 "  Dot: .tables .mvcc on|off .exit\n";
                } else std::cout << "unknown command\n";
                continue;
            }
            if (kw == "BEGIN")  { session = db.begin(); std::cout << "BEGIN (tx " << session << ")\n"; continue; }
            if (kw == "COMMIT") { if (session) { db.commit(session); std::cout << "COMMIT (tx " << session << ")\n"; session = 0; } else std::cout << "no active transaction\n"; continue; }
            if (kw == "ABORT" || kw == "ROLLBACK") { if (session) { db.abort(session); std::cout << "ABORT (tx " << session << ")\n"; session = 0; } else std::cout << "no active transaction\n"; continue; }

            if (session) {
                ExecResult r = db.run(stmt, session);
                if (r.is_query) print_rs(r.rs); else std::cout << r.message << "\n";
            } else {
                ExecResult r = db.run_autocommit(stmt);
                if (r.is_query) print_rs(r.rs); else std::cout << r.message << "\n";
            }
        } catch (std::exception& e) {
            std::cout << "ERROR: " << e.what() << "\n";
        }
    }
    db.close();
    std::cout << "bye\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Scripted demo of every required capability.
// ---------------------------------------------------------------------------
static void banner(const std::string& s) {
    std::cout << "\n========== " << s << " ==========\n";
}

// Two transactions grab opposite rows then each reach for the other's row,
// forming a waits-for cycle. The lock manager's DFS detects it and aborts one.
static void run_deadlock_demo(Database& db) {
    db.run_autocommit("INSERT INTO accounts VALUES (1, 100)");
    db.run_autocommit("INSERT INTO accounts VALUES (2, 200)");

    TxId ta = db.begin();
    TxId tb = db.begin();
    // Each transaction first locks its own row (via an in-place delete marker).
    db.run("DELETE FROM accounts WHERE id = 1", ta); // ta holds X lock on accounts#1
    db.run("DELETE FROM accounts WHERE id = 2", tb); // tb holds X lock on accounts#2

    std::atomic<int> aborted{0}, committed{0};
    auto worker = [&](TxId self, int other_id, const char* name) {
        try {
            // Reach for the row the OTHER transaction is holding.
            db.run("DELETE FROM accounts WHERE id = " + std::to_string(other_id), self);
            db.commit(self);
            committed++;
            std::cout << "  " << name << " (tx " << self << ") COMMITTED\n";
        } catch (DeadlockException& e) {
            std::cout << "  " << name << " (tx " << self << ") -> " << e.what() << "\n";
            db.abort(self);
            aborted++;
        } catch (std::exception& e) {
            std::cout << "  " << name << " error: " << e.what() << "\n";
            db.abort(self);
        }
    };

    std::thread th1(worker, ta, 2, "T_A");
    std::thread th2(worker, tb, 1, "T_B");
    th1.join();
    th2.join();
    std::cout << "  result: " << committed.load() << " committed, "
              << aborted.load() << " aborted (deadlock broken by aborting one)\n";
}

static void demo() {
    const std::string prefix = "/tmp/minidb_demo";
    std::remove((prefix + ".db").c_str());
    std::remove((prefix + ".wal").c_str());
    std::remove((prefix + ".catalog").c_str());

    {
        Database db(prefix, /*mvcc=*/true, /*buffer_pages=*/1024);

        banner("1. Storage + SQL: CREATE / INSERT / SELECT");
        db.run_autocommit("CREATE TABLE students (id INT PRIMARY KEY, name TEXT, age INT)");
        for (int i = 1; i <= 8; ++i)
            db.run_autocommit("INSERT INTO students VALUES (" + std::to_string(i) +
                              ", 'student_" + std::to_string(i) + "', " + std::to_string(18 + i) + ")");
        print_rs(db.run_autocommit("SELECT id, name, age FROM students WHERE age > 22").rs);

        banner("2. Cost-based optimizer: index scan vs sequential scan");
        std::cout << "-- equality on primary key (should pick Index Scan):\n";
        print_rs(db.run_autocommit("EXPLAIN SELECT name FROM students WHERE id = 5").rs);
        std::cout << "-- range predicate (should pick Seq Scan):\n";
        print_rs(db.run_autocommit("EXPLAIN SELECT name FROM students WHERE age > 20").rs);

        banner("3. JOIN across two tables");
        db.run_autocommit("CREATE TABLE enroll (id INT PRIMARY KEY, sid INT, course TEXT)");
        db.run_autocommit("INSERT INTO enroll VALUES (1, 2, 'DBMS')");
        db.run_autocommit("INSERT INTO enroll VALUES (2, 5, 'OS')");
        db.run_autocommit("INSERT INTO enroll VALUES (3, 2, 'Networks')");
        print_rs(db.run_autocommit(
            "EXPLAIN SELECT students.name, enroll.course FROM students "
            "JOIN enroll ON students.id = enroll.sid").rs);
        print_rs(db.run_autocommit(
            "SELECT students.name, enroll.course FROM students "
            "JOIN enroll ON students.id = enroll.sid").rs);

        banner("4. MVCC snapshot isolation (extension track B)");
        TxId reader = db.begin();   // snapshot taken now
        std::cout << "reader tx " << reader << " takes its snapshot\n";
        db.run_autocommit("INSERT INTO students VALUES (99, 'late_arrival', 40)");
        std::cout << "another transaction inserted id=99 and committed.\n";
        std::cout << "reader still should NOT see id=99 (snapshot isolation):\n";
        print_rs(db.run("SELECT id, name FROM students WHERE id = 99", reader).rs);
        db.commit(reader);
        TxId reader2 = db.begin();
        std::cout << "a NEW transaction " << reader2 << " takes a fresh snapshot and DOES see it:\n";
        print_rs(db.run("SELECT id, name FROM students WHERE id = 99", reader2).rs);
        db.commit(reader2);

        banner("5. Transactions: 2PL deadlock detection");
        db.run_autocommit("CREATE TABLE accounts (id INT PRIMARY KEY, bal INT)");
        run_deadlock_demo(db);

        banner("6. DELETE + durability checkpoint");
        print_rs(db.run_autocommit("DELETE FROM students WHERE id = 3").rs);
        std::cout << "rows with id=3 removed; committing and flushing to disk...\n";

        // IMPORTANT: clean close so pages + catalog are persisted before the
        // crash test below operates on a separate, deliberately un-flushed run.
        db.close();
    }

    banner("7. Crash recovery via WAL");
    {
        const std::string cprefix = "/tmp/minidb_crash";
        std::remove((cprefix + ".db").c_str());
        std::remove((cprefix + ".wal").c_str());
        std::remove((cprefix + ".catalog").c_str());

        Database db(cprefix, /*mvcc=*/true, /*buffer_pages=*/1024);
        db.run_autocommit("CREATE TABLE t (id INT PRIMARY KEY, v TEXT)");

        // Committed transaction - WAL has its COMMIT record.
        TxId t1 = db.begin();
        db.run("INSERT INTO t VALUES (1, 'committed_A')", t1);
        db.run("INSERT INTO t VALUES (2, 'committed_B')", t1);
        db.commit(t1);

        // Uncommitted transaction - no COMMIT record will ever be written.
        TxId t2 = db.begin();
        db.run("INSERT INTO t VALUES (3, 'uncommitted_C')", t2);
        std::cout << "t1 committed (id 1,2); t2 left uncommitted (id 3). Crashing now...\n";
        db.simulate_crash(); // drop dirty pages WITHOUT flushing
    }
    {
        const std::string cprefix = "/tmp/minidb_crash";
        std::cout << "reopening database -> running WAL recovery...\n";
        Database db(cprefix, /*mvcc=*/true, /*buffer_pages=*/1024);
        std::cout << "after recovery, committed rows survive and uncommitted are gone:\n";
        print_rs(db.run_autocommit("SELECT id, v FROM t").rs);
        db.close();
    }
    std::cout << "\nAll demos complete.\n";
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf; // flush on every write so progress is visible
    std::string arg = argc > 1 ? argv[1] : "";
    if (arg == "demo") { demo(); return 0; }
    return run_repl(arg.empty() ? "minidb_data" : arg);
}
