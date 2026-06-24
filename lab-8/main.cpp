// ============================================================================
// Lab 7 Test Harness: MVCC & Strict 2PL Transaction Engine
// Student: Pratham Onkar Singh
// ============================================================================

#include "transaction_engine.h"

#include <iostream>
#include <string>
#include <cstdlib>

using namespace mvcc_ledger;

namespace {

void printBanner(const std::string& testName) {
    std::cout << "\n============================================================\n"
              << " " << testName
              << "\n============================================================\n";
}

void requireTrue(bool condition, const std::string& successMessage) {
    if (condition) {
        std::cout << "  [VERIFIED] " << successMessage << "\n";
    } else {
        std::cerr << "  [CRITICAL CONCURRENCY FAULT] Failed: " << successMessage << "\n";
        std::exit(EXIT_FAILURE);
    }
}

// Helper to inspect ledger state cleanly without throwing exceptions
std::string queryLedger(LedgerCoordinator& coordinator, TxID tid, const std::string& accountKey) {
    std::string fetchedBalance;
    ExecStatus status = coordinator.readRecord(tid, accountKey, fetchedBalance);
    return (status == ExecStatus::Success) ? fetchedBalance : "<unallocated>";
}

} // anonymous namespace

int main() {
    LedgerCoordinator ledger;

    // ========================================================================
    printBanner("Scenario 1: MVCC Snapshot Isolation Verification");
    // ========================================================================
    {
        TxID txInit = ledger.beginTransaction();
        ledger.updateRecord(txInit, "acc_alice", "1000");
        requireTrue(ledger.commitTransaction(txInit) == ExecStatus::Success, "Base deposit 'acc_alice'=$1000 committed.");

        TxID txSnapshotReader = ledger.beginTransaction();
        std::cout << "  [TX#" << txSnapshotReader << "] Initial fetch acc_alice = $" 
                  << queryLedger(ledger, txSnapshotReader, "acc_alice") << "\n";

        TxID txConcurrentWriter = ledger.beginTransaction();
        ledger.updateRecord(txConcurrentWriter, "acc_alice", "2500");
        requireTrue(ledger.commitTransaction(txConcurrentWriter) == ExecStatus::Success, "Concurrent writer mutates acc_alice=$2500.");

        std::string repeatedRead = queryLedger(ledger, txSnapshotReader, "acc_alice");
        std::cout << "  [TX#" << txSnapshotReader << "] Repeat fetch acc_alice = $" << repeatedRead << " (Snapshot View)\n";
        requireTrue(repeatedRead == "1000", "Reader successfully isolated from unobserved concurrent commits.");

        TxID txPostCommit = ledger.beginTransaction();
        requireTrue(queryLedger(ledger, txPostCommit, "acc_alice") == "2500", "Subsequent transaction observes mutated acc_alice=$2500.");

        ledger.commitTransaction(txSnapshotReader);
        ledger.commitTransaction(txPostCommit);
    }

    // ========================================================================
    printBanner("Scenario 2: Strict Two-Phase Locking (2PL) Contention");
    // ========================================================================
    {
        TxID txTraderA = ledger.beginTransaction();
        TxID txTraderB = ledger.beginTransaction();

        requireTrue(ledger.updateRecord(txTraderA, "asset_gold", "50") == ExecStatus::Success, "Trader A acquires exclusive lock on asset_gold.");
        requireTrue(ledger.updateRecord(txTraderB, "asset_gold", "90") == ExecStatus::WaitBlocked, "Trader B write mutation correctly BLOCKED.");

        ledger.rollbackTransaction(txTraderA);
        requireTrue(ledger.queryTxLifecycle(txTraderA) == Lifecycle::RolledBack, "Trader A aborts, relinquishing exclusive write locks.");
        
        requireTrue(ledger.updateRecord(txTraderB, "asset_gold", "90") == ExecStatus::Success, "Trader B successfully claims uncontentious asset_gold lock.");
        requireTrue(ledger.commitTransaction(txTraderB) == ExecStatus::Success, "Trader B commits transaction safely.");

        TxID txAuditor = ledger.beginTransaction();
        requireTrue(queryLedger(ledger, txAuditor, "asset_gold") == "90", "asset_gold mutation correctly persisted as $90.");
        ledger.commitTransaction(txAuditor);
    }

    // ========================================================================
    printBanner("Scenario 3: Wait-For Graph Deadlock Detection & Victim Selection");
    // ========================================================================
    {
        TxID txAlpha = ledger.beginTransaction();
        TxID txBeta  = ledger.beginTransaction();

        requireTrue(ledger.updateRecord(txAlpha, "escrow_x", "HOLD_ALPHA") == ExecStatus::Success, "TX-Alpha locks escrow_x.");
        requireTrue(ledger.updateRecord(txBeta,  "escrow_y", "HOLD_BETA")  == ExecStatus::Success, "TX-Beta locks escrow_y.");

        requireTrue(ledger.updateRecord(txAlpha, "escrow_y", "CLAIM_ALPHA") == ExecStatus::WaitBlocked, "TX-Alpha enters wait queue behind TX-Beta.");

        ExecStatus cycleOutcome = ledger.updateRecord(txBeta, "escrow_x", "CLAIM_BETA");
        std::cout << "  [TX#" << txBeta << "] Requests escrow_x -> Outcome: " << formatStatus(cycleOutcome)
                  << " | Terminated Victim = TX#" << ledger.getLastAbortedTx() << "\n";

        requireTrue(cycleOutcome == ExecStatus::ForceAborted, "TX-Beta execution forcefully terminated due to circular deadlock cycle.");
        requireTrue(ledger.getLastAbortedTx() == txBeta, "Deadlock resolver accurately targeted younger transaction.");
        requireTrue(ledger.queryTxLifecycle(txBeta) == Lifecycle::RolledBack, "TX-Beta lifecycle transitioned to RolledBack.");

        requireTrue(ledger.updateRecord(txAlpha, "escrow_y", "CLAIM_ALPHA") == ExecStatus::Success, "TX-Alpha inherits escrow_y lock post victim teardown.");
        requireTrue(ledger.commitTransaction(txAlpha) == ExecStatus::Success, "TX-Alpha successfully commits dual escrow claim.");

        TxID txStateVerify = ledger.beginTransaction();
        requireTrue(queryLedger(ledger, txStateVerify, "escrow_x") == "HOLD_ALPHA" && 
                    queryLedger(ledger, txStateVerify, "escrow_y") == "CLAIM_ALPHA", 
                    "Deadlock resolution state accurately solidified.");
        ledger.commitTransaction(txStateVerify);
    }

    // ========================================================================
    printBanner("Scenario 4: First-Updater-Wins Concurrency Enforcement");
    // ========================================================================
    {
        TxID txSeed = ledger.beginTransaction();
        ledger.updateRecord(txSeed, "stock_tesla", "100");
        ledger.commitTransaction(txSeed);

        TxID txBroker1 = ledger.beginTransaction();
        TxID txBroker2 = ledger.beginTransaction();

        requireTrue(ledger.updateRecord(txBroker1, "stock_tesla", "110") == ExecStatus::Success, "Broker 1 pins stock_tesla=110.");
        requireTrue(ledger.updateRecord(txBroker2, "stock_tesla", "120") == ExecStatus::WaitBlocked, "Broker 2 blocks waiting on Broker 1.");

        requireTrue(ledger.commitTransaction(txBroker1) == ExecStatus::Success, "Broker 1 commits stock_tesla=110.");

        requireTrue(ledger.updateRecord(txBroker2, "stock_tesla", "120") == ExecStatus::Success, "Broker 2 unblocks and applies local mutation.");
        
        ExecStatus commitAttempt = ledger.commitTransaction(txBroker2);
        std::cout << "  [TX#" << txBroker2 << "] Commit Attempt -> Outcome: " << formatStatus(commitAttempt) << "\n";

        requireTrue(commitAttempt == ExecStatus::CommitConflict, "Broker 2 commit rejected due to unobserved concurrent overwrite conflict.");

        TxID txAudit = ledger.beginTransaction();
        requireTrue(queryLedger(ledger, txAudit, "stock_tesla") == "110", "First-updater (Broker 1) state preserved.");
        ledger.commitTransaction(txAudit);
    }

    // ========================================================================
    printBanner("Scenario 5: Garbage Collection & Obsolete Snapshot Pruning");
    // ========================================================================
    {
        std::size_t initialVersions = ledger.getTotalStoredVersions();
        std::size_t reclaimed       = ledger.collectGarbage();
        std::size_t remaining       = ledger.getTotalStoredVersions();

        std::cout << "  Global Version Ledger: " << initialVersions << " -> " << remaining
                  << " (Purged historical snapshots: " << reclaimed << ")\n";

        requireTrue(reclaimed > 0, "Vacuum engine successfully swept unreachable historical versions.");
        requireTrue(initialVersions - reclaimed == remaining, "Version ledger conservation arithmetic verified.");

        TxID txFinal = ledger.beginTransaction();
        requireTrue(queryLedger(ledger, txFinal, "acc_alice")   == "2500", "acc_alice=$2500 intact post-GC.");
        requireTrue(queryLedger(ledger, txFinal, "stock_tesla") == "110",  "stock_tesla=110 intact post-GC.");
        ledger.commitTransaction(txFinal);
    }

    std::cout << "\n>>> ALL FINANCIAL LEDGER CONCURRENCY TESTS PASSED <<<\n";
    return EXIT_SUCCESS;
}