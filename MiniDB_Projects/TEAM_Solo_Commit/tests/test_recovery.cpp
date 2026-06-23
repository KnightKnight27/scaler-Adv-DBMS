// Crash recovery: committed work survives a crash; an uncommitted transaction is rolled back.
#include "database.h"
#include "test_util.h"

using namespace minidb;

static size_t rowcount(Database& db) { return db.Execute("SELECT * FROM acct").rows.size(); }

int main() {
    std::remove("/tmp/minidb_test_rec.db");
    std::remove("/tmp/minidb_test_rec.db.wal");

    {  // phase 1: commit some work, then "crash" with an uncommitted transaction open
        Database db("/tmp/minidb_test_rec.db", 16);
        db.Execute("CREATE TABLE acct (id INT PRIMARY KEY, bal INT)");
        db.Execute("INSERT INTO acct VALUES (1, 100), (2, 200)");
        Transaction* t1 = db.BeginTxn();
        db.Execute("INSERT INTO acct VALUES (3, 300)", t1);
        db.CommitTxn(t1);

        Transaction* t2 = db.BeginTxn();
        db.Execute("INSERT INTO acct VALUES (99, 9999)", t2);  // never committed
        db.Execute("DELETE FROM acct WHERE id = 1", t2);       // never committed
        db.Flush();
        // destructor here = crash before t2 commits
    }

    {  // phase 2: reopen -> recovery replays the WAL
        Database db("/tmp/minidb_test_rec.db", 16);
        CHECK(rowcount(db) == 3);  // ids 1,2,3
        CHECK(db.Execute("SELECT * FROM acct WHERE id = 99").rows.empty());      // insert rolled back
        CHECK(db.Execute("SELECT * FROM acct WHERE id = 1").rows.size() == 1);   // delete rolled back
        CHECK(db.Execute("SELECT * FROM acct WHERE id = 3").rows.size() == 1);   // commit survived
    }

    return minidb_test::Done("recovery");
}
