// Tests for WAL + crash recovery: committed survives, uncommitted rolls back.
#include <filesystem>

#include "check.h"
#include "recovery.h"

using namespace minidb;

int main() {
    const std::string wal = "rec_test.wal";
    std::filesystem::remove(wal);
    {
        LogManager log(wal);
        log.logBegin(1);
        log.logUpdate(1, 1, "~", "Alice");
        log.logUpdate(1, 2, "~", "Bob");
        log.logCommit(1);
        log.logBegin(2);
        log.logUpdate(2, 1, "Alice", "HACKED");
        log.logUpdate(2, 3, "~", "Ghost");
        // T2 never commits: simulated crash here
    }

    RecoveryManager rec(wal);
    int redone = 0, undone = 0;
    auto store = rec.recover(redone, undone);

    CHECK(store.size() == 2);
    CHECK(store[1] == "Alice");   // T2's update was undone
    CHECK(store[2] == "Bob");
    CHECK(store.count(3) == 0);   // T2's insert was undone
    CHECK(redone == 2);
    CHECK(undone == 2);

    std::filesystem::remove(wal);
    REPORT();
}
