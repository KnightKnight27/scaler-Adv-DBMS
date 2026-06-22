// Crash-recovery test. Exercises the WAL: committed transactions survive a
// simulated crash; an uncommitted transaction is rolled back (never replayed);
// and an explicit ABORT undoes its changes in-session.
#include <cassert>
#include <cstdio>
#include <iostream>
#include "engine/database.h"

using namespace minidb;

static void cleanup(const std::string &b) {
    std::remove((b + ".db").c_str());
    std::remove((b + ".wal").c_str());
}

int main() {
    const std::string base = "test_rec";
    cleanup(base);

    // ---- Phase 1: do work, then "crash" with one uncommitted transaction ----
    {
        Database db(base);
        assert(db.execute("CREATE TABLE accounts (id INT, bal INT, PRIMARY KEY (id))").ok);
        assert(db.execute("INSERT INTO accounts VALUES (1, 100)").ok);   // auto-commit
        assert(db.execute("INSERT INTO accounts VALUES (2, 200)").ok);

        // Explicit committed transaction.
        Transaction *t = db.begin();
        assert(db.execute("INSERT INTO accounts VALUES (3, 300)", t).ok);
        db.commit(t);

        // Explicit ABORT must undo its insert immediately.
        Transaction *a = db.begin();
        assert(db.execute("INSERT INTO accounts VALUES (5, 500)", a).ok);
        db.abort(a);
        auto chk = db.execute("SELECT id FROM accounts WHERE id = 5");
        assert(chk.ok && chk.rows.empty()); // aborted insert gone

        // Uncommitted transaction: insert then "crash" (never commit).
        Transaction *u = db.begin();
        assert(db.execute("INSERT INTO accounts VALUES (4, 400)", u).ok);
        // Intentionally do NOT commit `u`. Leaving scope simulates a crash;
        // the WAL has BEGIN+INSERT for `u` but no COMMIT.
    }

    // ---- Phase 2: reopen -> recovery replays the WAL ----
    {
        Database db(base);
        auto all = db.execute("SELECT id FROM accounts");
        assert(all.ok);
        // ids 1,2,3 committed; 4 uncommitted (lost); 5 aborted (lost) => 3 rows.
        assert(all.rows.size() == 3);

        assert(db.execute("SELECT id FROM accounts WHERE id = 3").rows.size() == 1);
        assert(db.execute("SELECT id FROM accounts WHERE id = 4").rows.empty());
        assert(db.execute("SELECT id FROM accounts WHERE id = 5").rows.empty());
    }

    cleanup(base);
    std::cout << "[OK] recovery: committed txns survive crash; uncommitted & "
                 "aborted txns rolled back (WAL redo/undo) verified" << std::endl;
    return 0;
}
