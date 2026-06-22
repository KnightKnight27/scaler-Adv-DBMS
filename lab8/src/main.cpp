#include <iostream>
#include <optional>
#include <string>

#include "txn/TransactionManager.h"

namespace {

using txn::OpStatus;
using txn::TransactionManager;
using txn::TxnId;
using txn::Value;

std::string show(const std::optional<Value>& v) {
    return v ? std::to_string(*v) : std::string("(none)");
}

void banner(const std::string& title) {
    std::cout << "\n========== " << title << " ==========\n";
}

// ---- Scenario 1: MVCC version chains give each reader a stable snapshot ----
void mvccDemo(TransactionManager& tm) {
    banner("1. MVCC version chains / snapshot reads");

    TxnId seed = tm.begin();
    tm.write(seed, "A", 100);
    tm.commit(seed);

    TxnId reader = tm.begin(/*readOnly=*/true);  // takes a snapshot of "A" = 100
    std::cout << "reader sees A = " << show(tm.snapshotRead(reader, "A")) << '\n';

    TxnId writer = tm.begin();
    tm.write(writer, "A", 999);
    tm.commit(writer);

    std::cout << "after writer commits 999:\n";
    std::cout << "  old reader still sees A = " << show(tm.snapshotRead(reader, "A"))
              << "  (its snapshot predates the new version)\n";

    TxnId fresh = tm.begin(/*readOnly=*/true);
    std::cout << "  fresh reader sees A = " << show(tm.snapshotRead(fresh, "A")) << '\n';
    tm.commit(reader);
    tm.commit(fresh);

    std::cout << "version chain for A (newest first):\n";
    tm.dumpVersions(std::cout);
}

// ---- Scenario 2: Strict 2PL — an X lock blocks a conflicting reader -------
void strict2plDemo(TransactionManager& tm) {
    banner("2. Strict 2PL: write lock blocks a reader until commit");

    TxnId seed = tm.begin();
    tm.write(seed, "X", 10);
    tm.commit(seed);

    TxnId t1 = tm.begin();
    std::cout << "T" << t1 << " write X=20 -> " << txn::toString(tm.write(t1, "X", 20)) << '\n';

    TxnId t2 = tm.begin();
    txn::ReadResult r = tm.read(t2, "X");
    std::cout << "T" << t2 << " read X -> " << txn::toString(r.status)
              << " (blocked by T" << t1 << "'s X lock)\n";

    std::cout << "T" << t1 << " commits, releasing its locks\n";
    tm.commit(t1);

    r = tm.read(t2, "X");
    std::cout << "T" << t2 << " retries read X -> " << txn::toString(r.status)
              << ", value = " << show(r.value) << '\n';
    tm.commit(t2);
}

// ---- Scenario 3: deadlock detection aborts a victim to break the cycle ----
void deadlockDemo(TransactionManager& tm) {
    banner("3. Deadlock detection (waits-for cycle)");

    TxnId t1 = tm.begin();
    TxnId t2 = tm.begin();

    std::cout << "T" << t1 << " write P -> " << txn::toString(tm.write(t1, "P", 1)) << '\n';
    std::cout << "T" << t2 << " write Q -> " << txn::toString(tm.write(t2, "Q", 1)) << '\n';

    std::cout << "T" << t1 << " write Q -> " << txn::toString(tm.write(t1, "Q", 2))
              << " (waits for T" << t2 << ")\n";
    OpStatus s = tm.write(t2, "P", 2);  // closes the cycle T1->T2->T1
    std::cout << "T" << t2 << " write P -> " << txn::toString(s) << '\n';

    std::cout << "outcome: T" << t1 << " is " << txn::toString(tm.state(t1)) << ", T" << t2
              << " is " << txn::toString(tm.state(t2)) << '\n';

    // The survivor's blocked write can now proceed.
    std::cout << "T" << t1 << " retries write Q -> " << txn::toString(tm.write(t1, "Q", 2)) << '\n';
    tm.commit(t1);
    std::cout << "final: T" << t1 << " is " << txn::toString(tm.state(t1)) << '\n';
}

}  // namespace

int main() {
    try {
        TransactionManager tm(std::cout);
        mvccDemo(tm);
        strict2plDemo(tm);
        deadlockDemo(tm);
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}
