// M5 test — crash recovery. The exact scenario the rubric asks us to
// demonstrate: a crash, the recovery process, and preservation of committed
// transactions (while uncommitted and aborted ones leave no trace).
#include "wal.hpp"
#include <cassert>
#include <cstdio>
#include <string>

using namespace minidb;

int main() {
    const std::string logf = "minidb_wal_test.log";
    std::remove(logf.c_str());

    // --- phase 1: do some work, then CRASH mid-flight ----------------------
    {
        WALStore db(logf);

        uint64_t t1 = db.begin();           // will COMMIT
        db.put(t1, "alice", "1000");
        db.put(t1, "bob", "500");
        db.commit(t1);

        uint64_t t2 = db.begin();           // will ABORT
        db.put(t2, "carol", "9999");
        db.abort(t2);

        uint64_t t3 = db.begin();           // never commits: in-flight at crash
        db.put(t3, "dave", "7777");

        // sanity before crash
        std::string v;
        assert(db.get("alice", &v) && v == "1000");
        assert(!db.get("carol", &v));       // aborted -> never visible
        assert(!db.get("dave", &v));        // uncommitted -> not visible

        db.crash();                          // <-- volatile state wiped
        assert(!db.get("alice", &v) && "crash cleared the in-memory store");
        std::printf("[M5] crash wiped volatile state (alice gone from memory)\n");
    }

    // --- phase 2: reopen on the same log and RECOVER -----------------------
    {
        WALStore db(logf);
        int redone = db.recover();
        std::printf("[M5] recovery replayed %d committed update(s)\n", redone);

        std::string v;
        // committed T1 survives
        assert(db.get("alice", &v) && v == "1000");
        assert(db.get("bob", &v)   && v == "500");
        // aborted T2 and in-flight T3 do NOT
        assert(!db.get("carol", &v) && "aborted txn must not be recovered");
        assert(!db.get("dave", &v)  && "uncommitted txn must not be recovered");
        std::printf("[M5] committed T1 preserved (alice=1000, bob=500); "
                    "aborted/uncommitted discarded  OK\n");
    }

    std::remove(logf.c_str());
    std::printf("[M5] WAL + crash recovery: ALL CHECKS PASSED\n");
    return 0;
}
