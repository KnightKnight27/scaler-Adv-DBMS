// Demonstrates write-ahead logging and crash recovery: a committed transaction
// survives a crash, while an uncommitted one is rolled back.
#include <filesystem>
#include <iostream>

#include "recovery.h"

using namespace minidb;

int main() {
    const std::string wal = "recovery_demo.wal";
    std::filesystem::remove(wal);
    LogManager log(wal);

    // T1 inserts two rows and COMMITS.  ("~" means the key did not exist before)
    log.logBegin(1);
    log.logUpdate(1, 1, "~", "Alice");
    log.logUpdate(1, 2, "~", "Bob");
    log.logCommit(1);

    // T2 changes key 1 and inserts key 3 ... then the process CRASHES with no COMMIT.
    log.logBegin(2);
    log.logUpdate(2, 1, "Alice", "HACKED");
    log.logUpdate(2, 3, "~", "Ghost");
    std::cout << "--- simulated crash: T2 never committed ---\n";

    RecoveryManager rec(wal);
    int redone = 0, undone = 0;
    auto store = rec.recover(redone, undone);

    std::cout << "recovery: redone " << redone << " committed update(s), undone "
              << undone << " uncommitted update(s)\n";
    std::cout << "database state after recovery:\n";
    for (const auto& kv : store) std::cout << "  key " << kv.first << " = " << kv.second << "\n";

    bool ok = store.size() == 2 && store[1] == "Alice" && store[2] == "Bob" && store.count(3) == 0;
    std::cout << (ok ? "OK: committed T1 preserved, uncommitted T2 rolled back\n"
                     : "ERROR: unexpected recovered state\n");
    return ok ? 0 : 1;
}
