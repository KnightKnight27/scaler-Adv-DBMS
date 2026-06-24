/**
 * Lab 6 — Transaction Manager Demo: MVCC + 2PL + Deadlock Detection
 */

#include "transaction_manager.h"
#include <iostream>
#include <string>

// ─────────────────────────────────────────────────
// Test 1: Basic MVCC Version Chains
// ─────────────────────────────────────────────────
void test_mvcc_versions() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Test 1: MVCC Version Chains                                ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    TransactionManager tm;

    // T1 writes initial values
    TxnId t1 = tm.begin();
    tm.write(t1, 1, "Alice_v1");
    tm.write(t1, 2, "Bob_v1");
    tm.commit(t1);

    // T2 starts, reads, sees committed values
    TxnId t2 = tm.begin();

    // T3 starts concurrently and updates
    TxnId t3 = tm.begin();
    tm.write(t3, 1, "Alice_v2");

    // T2 reads — should see v1 (T3 hasn't committed)
    std::cout << "\n  T2 reads row 1 (T3's write is uncommitted):" << std::endl;
    auto val = tm.read(t2, 1);
    std::cout << "  → T2 sees: " << (val ? *val : "NULL") << std::endl;

    // T3 commits
    tm.commit(t3);

    // T2 reads again — still sees v1 (snapshot isolation!)
    std::cout << "\n  T2 reads row 1 again (T3 committed, but T2's snapshot is older):" << std::endl;
    val = tm.read(t2, 1);
    std::cout << "  → T2 still sees: " << (val ? *val : "NULL") << std::endl;

    tm.commit(t2);

    // T4 starts — should see v2 (T3's committed version)
    TxnId t4 = tm.begin();
    std::cout << "\n  T4 reads row 1 (fresh snapshot, sees T3's commit):" << std::endl;
    val = tm.read(t4, 1);
    std::cout << "  → T4 sees: " << (val ? *val : "NULL") << std::endl;
    tm.commit(t4);

    // Show version chain
    std::cout << "\n  Version chain for row 1:" << std::endl;
    tm.print_version_chain(1);
}

// ─────────────────────────────────────────────────
// Test 2: Strict 2PL Lock Behavior
// ─────────────────────────────────────────────────
void test_strict_2pl() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Test 2: Strict Two-Phase Locking                           ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    TransactionManager tm;

    // Setup: write initial data
    TxnId setup = tm.begin();
    tm.write(setup, 100, "balance=1000");
    tm.write(setup, 101, "balance=2000");
    tm.commit(setup);

    // T1 reads row 100 (gets S lock)
    TxnId t1 = tm.begin();
    tm.read(t1, 100);  // S lock on row 100

    // T2 also reads row 100 (S lock is compatible)
    TxnId t2 = tm.begin();
    tm.read(t2, 100);  // S lock — compatible with T1's S lock

    std::cout << "\n  Both T1 and T2 hold shared locks on row 100:" << std::endl;
    tm.print_lock_table();

    // T1 writes row 101 (gets X lock)
    tm.write(t1, 101, "balance=1500");

    // T2 writes row 100 (needs X lock — must wait for T1)
    std::cout << "\n  T2 tries to write row 100 (needs X lock, T1 has S lock):" << std::endl;
    tm.write(t2, 100, "balance=900");

    std::cout << "\n  Lock table after write attempts:" << std::endl;
    tm.print_lock_table();

    // T1 commits — releases all locks (strict 2PL)
    std::cout << "\n  T1 commits (strict 2PL: all locks released at commit):" << std::endl;
    tm.commit(t1);
    tm.print_lock_table();

    tm.commit(t2);

    tm.print_transaction_table();
}

// ─────────────────────────────────────────────────
// Test 3: Deadlock Detection
// ─────────────────────────────────────────────────
void test_deadlock_detection() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Test 3: Deadlock Detection (Wait-For Graph + DFS)          ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    // Direct deadlock detector test
    DeadlockDetector dd;

    std::cout << "\n--- Scenario A: Simple cycle (T1 → T2 → T1) ---" << std::endl;
    dd.add_edge(1, 2);  // T1 waits for T2
    dd.add_edge(2, 1);  // T2 waits for T1

    dd.print_graph();

    std::vector<TxnId> cycle;
    bool has_deadlock = dd.detect(cycle);
    std::cout << "  Deadlock detected: " << (has_deadlock ? "YES" : "NO") << std::endl;
    if (has_deadlock) {
        std::cout << "  Cycle: ";
        for (TxnId t : cycle) std::cout << "T" << t << " ";
        std::cout << std::endl;
        std::cout << "  Victim (youngest): T" << dd.choose_victim(cycle) << std::endl;
    }

    // Clear and test a longer cycle
    dd.remove_transaction(1);
    dd.remove_transaction(2);

    std::cout << "\n--- Scenario B: Three-way deadlock (T1 → T2 → T3 → T1) ---" << std::endl;
    dd.add_edge(1, 2);
    dd.add_edge(2, 3);
    dd.add_edge(3, 1);

    dd.print_graph();

    cycle.clear();
    has_deadlock = dd.detect(cycle);
    std::cout << "  Deadlock detected: " << (has_deadlock ? "YES" : "NO") << std::endl;
    if (has_deadlock) {
        std::cout << "  Cycle: ";
        for (TxnId t : cycle) std::cout << "T" << t << " ";
        std::cout << std::endl;
        std::cout << "  Victim (youngest): T" << dd.choose_victim(cycle) << std::endl;
    }

    // No deadlock scenario
    dd.remove_transaction(1);
    dd.remove_transaction(2);
    dd.remove_transaction(3);

    std::cout << "\n--- Scenario C: No deadlock (T1 → T2, T3 → T2) ---" << std::endl;
    dd.add_edge(1, 2);
    dd.add_edge(3, 2);

    dd.print_graph();

    cycle.clear();
    has_deadlock = dd.detect(cycle);
    std::cout << "  Deadlock detected: " << (has_deadlock ? "YES" : "NO") << std::endl;
}

// ─────────────────────────────────────────────────
// Test 4: Transaction Abort & Rollback
// ─────────────────────────────────────────────────
void test_abort_rollback() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Test 4: Transaction Abort & Rollback                       ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    TransactionManager tm;

    // Setup
    TxnId setup = tm.begin();
    tm.write(setup, 1, "original_value");
    tm.commit(setup);

    // T1 modifies the row
    TxnId t1 = tm.begin();
    tm.write(t1, 1, "modified_by_t1");

    // Before abort, T1 can read its own modification
    std::cout << "\n  T1 reads its own write:" << std::endl;
    auto val = tm.read(t1, 1);
    std::cout << "  → T1 sees: " << (val ? *val : "NULL") << std::endl;

    // Abort T1
    std::cout << "\n  Aborting T1..." << std::endl;
    tm.abort(t1);

    // T2 starts — should see original value (T1 was aborted)
    TxnId t2 = tm.begin();
    std::cout << "\n  T2 reads row 1 after T1 abort:" << std::endl;
    val = tm.read(t2, 1);
    std::cout << "  → T2 sees: " << (val ? *val : "NULL") << std::endl;
    std::cout << "  (Should be 'original_value' since T1 was aborted)" << std::endl;
    tm.commit(t2);

    tm.print_transaction_table();
}

// ─────────────────────────────────────────────────
// Test 5: Complete Scenario (Banking Transfer)
// ─────────────────────────────────────────────────
void test_banking_scenario() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Test 5: Banking Scenario (Transfer Between Accounts)       ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    TransactionManager tm;

    // Setup accounts
    TxnId setup = tm.begin();
    tm.write(setup, 1, "account_A:1000");
    tm.write(setup, 2, "account_B:2000");
    tm.write(setup, 3, "account_C:500");
    tm.commit(setup);

    std::cout << "\n  Initial state:" << std::endl;
    TxnId reader = tm.begin();
    tm.read(reader, 1);
    tm.read(reader, 2);
    tm.read(reader, 3);
    tm.commit(reader);

    // Transfer 1: A → B (500)
    std::cout << "\n  --- Transfer: A → B ($500) ---" << std::endl;
    TxnId transfer1 = tm.begin();
    tm.read(transfer1, 1);    // read A (S lock)
    tm.read(transfer1, 2);    // read B (S lock)
    tm.write(transfer1, 1, "account_A:500");    // debit A (X lock)
    tm.write(transfer1, 2, "account_B:2500");   // credit B (X lock)
    tm.commit(transfer1);

    // Transfer 2: B → C (300) — concurrent with a reader
    std::cout << "\n  --- Transfer: B → C ($300) + concurrent reader ---" << std::endl;
    TxnId transfer2 = tm.begin();
    TxnId concurrent_reader = tm.begin();

    tm.write(transfer2, 2, "account_B:2200");    // debit B
    tm.write(transfer2, 3, "account_C:800");     // credit C

    // Reader should see pre-transfer values (snapshot isolation)
    std::cout << "\n  Concurrent reader (before transfer2 commits):" << std::endl;
    auto a = tm.read(concurrent_reader, 1);
    auto b = tm.read(concurrent_reader, 2);
    auto c = tm.read(concurrent_reader, 3);
    std::cout << "  → A=" << (a ? *a : "NULL")
              << ", B=" << (b ? *b : "NULL")
              << ", C=" << (c ? *c : "NULL") << std::endl;

    tm.commit(transfer2);
    tm.commit(concurrent_reader);

    // Final state
    std::cout << "\n  Final state:" << std::endl;
    TxnId final_reader = tm.begin();
    a = tm.read(final_reader, 1);
    b = tm.read(final_reader, 2);
    c = tm.read(final_reader, 3);
    std::cout << "  → A=" << (a ? *a : "NULL")
              << ", B=" << (b ? *b : "NULL")
              << ", C=" << (c ? *c : "NULL") << std::endl;
    tm.commit(final_reader);

    tm.print_transaction_table();
}

// ─────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────
int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Lab 6: Transaction Manager                                ║" << std::endl;
    std::cout << "║  MVCC + Strict 2PL + Deadlock Detection                    ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    test_mvcc_versions();
    test_strict_2pl();
    test_deadlock_detection();
    test_abort_rollback();
    test_banking_scenario();

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Lab 6 Complete!                                            ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    return 0;
}
