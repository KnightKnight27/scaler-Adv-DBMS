// main.cpp — ADBMS Lab 6 test runner / 24bcs10213 Jatin Chulet
//
// Executes tests for the transaction engine simulating five distinct scenarios:
//   1. MVCC Verification: Ensuring isolated snapshots persist during concurrent writes.
//   2. 2PL Locks: Validating that exclusive locks block subsequent updates.
//   3. Deadlock Breaks: Ensuring cyclic dependencies are resolved by rolling back the newest transaction.
//   4. Concurrency Faults: Confirming serialization failures when overwriting unseen data.
//   5. Vacuum/GC: Verifying that inaccessible historical data gets cleanly swept.

#include "transaction_engine.h"
#include <cstdlib>
#include <iostream>
#include <string>

using namespace db_core;

namespace {

void print_header(const std::string& title) {
    std::cout << "\n=== " << title << " ===\n";
}

void validate(bool condition, const std::string& msg) {
    std::cout << (condition ? "  [PASS] " : "  [FAIL] ") << msg << "\n";
    if (!condition) std::exit(EXIT_FAILURE);
}

// Helper to fetch data safely and fallback to <null>
std::string fetch_val(TransactionEngine& engine, TransactionID t_id, const std::string& key) {
    std::string val;
    OpStatus status = engine.fetch_record(t_id, key, val);
    return status == OpStatus::Success ? val : "<null>";
}

}  // anonymous namespace

int main() {
    TransactionEngine db;

    // --------------------------------------------------------------------
    print_header("Test 1: MVCC snapshot consistency");
    // --------------------------------------------------------------------
    {
        TransactionID init_tx = db.start_tx();
        db.update_record(init_tx, "alpha", "100");
        validate(db.commit_tx(init_tx) == OpStatus::Success, "Initial record 'alpha'=100 committed");

        TransactionID reader_tx = db.start_tx();
        std::cout << "  TX-" << reader_tx << " fetches alpha = " << fetch_val(db, reader_tx, "alpha") << "\n";

        TransactionID writer_tx = db.start_tx();
        db.update_record(writer_tx, "alpha", "200");
        validate(db.commit_tx(writer_tx) == OpStatus::Success, "Concurrent writer updates alpha=200");

        std::string res = fetch_val(db, reader_tx, "alpha");
        std::cout << "  TX-" << reader_tx << " fetches alpha again = " << res << " (isolated view)\n";
        validate(res == "100", "Reader still observes historical snapshot alpha=100");

        TransactionID new_reader = db.start_tx();
        validate(fetch_val(db, new_reader, "alpha") == "200", "Fresh transaction sees alpha=200");

        db.commit_tx(reader_tx);
        db.commit_tx(new_reader);
    }

    // --------------------------------------------------------------------
    print_header("Test 2: Strict 2PL concurrency locks");
    // --------------------------------------------------------------------
    {
        TransactionID tx_a = db.start_tx();
        TransactionID tx_b = db.start_tx();

        validate(db.update_record(tx_a, "beta", "1") == OpStatus::Success, "TX-A acquires exclusive lock on beta");
        validate(db.update_record(tx_b, "beta", "2") == OpStatus::Waiting, "TX-B update gets WAITING status");

        db.rollback_tx(tx_a);
        validate(db.get_state(tx_a) == State::Failed, "TX-A rolled back, releasing lock");
        validate(db.update_record(tx_b, "beta", "2") == OpStatus::Success, "TX-B successfully acquires beta lock");
        validate(db.commit_tx(tx_b) == OpStatus::Success, "TX-B commits cleanly");

        TransactionID verify_tx = db.start_tx();
        validate(fetch_val(db, verify_tx, "beta") == "2", "Beta successfully persisted as 2");
        db.commit_tx(verify_tx);
    }

    // --------------------------------------------------------------------
    print_header("Test 3: Cyclic deadlock detection & resolution");
    // --------------------------------------------------------------------
    {
        TransactionID tx_x = db.start_tx();
        TransactionID tx_y = db.start_tx();

        validate(db.update_record(tx_x, "foo", "X1") == OpStatus::Success, "TX-X locks foo");
        validate(db.update_record(tx_y, "bar", "Y1") == OpStatus::Success, "TX-Y locks bar");

        validate(db.update_record(tx_x, "bar", "X2") == OpStatus::Waiting, "TX-X waits on TX-Y for bar");

        OpStatus res = db.update_record(tx_y, "foo", "Y2");
        std::cout << "  TX-Y requests foo -> " << status_to_string(res)
                  << " | Aborted Target = TX-" << db.get_latest_aborted() << "\n";

        validate(res == OpStatus::RolledBack, "TX-Y forcefully aborted due to deadlock cycle");
        validate(db.get_latest_aborted() == tx_y, "Target was correctly identified as the newer transaction");
        validate(db.get_state(tx_y) == State::Failed, "TX-Y is in Failed state");

        validate(db.update_record(tx_x, "bar", "X2") == OpStatus::Success, "TX-X assumes lock over bar after TX-Y dies");
        validate(db.commit_tx(tx_x) == OpStatus::Success, "TX-X commits foo=X1, bar=X2");

        TransactionID check_tx = db.start_tx();
        validate(fetch_val(db, check_tx, "foo") == "X1" && fetch_val(db, check_tx, "bar") == "X2", "Deadlock resolution values persisted");
        db.commit_tx(check_tx);
    }

    // --------------------------------------------------------------------
    print_header("Test 4: First-updater-wins constraint");
    // --------------------------------------------------------------------
    {
        TransactionID base_tx = db.start_tx();
        db.update_record(base_tx, "gamma", "0");
        db.commit_tx(base_tx);

        TransactionID t1 = db.start_tx();
        TransactionID t2 = db.start_tx();

        validate(db.update_record(t1, "gamma", "10") == OpStatus::Success, "T1 updates gamma=10");
        validate(db.update_record(t2, "gamma", "20") == OpStatus::Waiting, "T2 waits behind T1");

        validate(db.commit_tx(t1) == OpStatus::Success, "T1 commits gamma=10");

        validate(db.update_record(t2, "gamma", "20") == OpStatus::Success, "T2 lock granted post T1 commit");
        OpStatus commit_res = db.commit_tx(t2);
        std::cout << "  T2 commit attempt -> " << status_to_string(commit_res) << "\n";

        validate(commit_res == OpStatus::ConflictError, "T2 block rejected due to concurrent mutation conflict");

        TransactionID verify_t = db.start_tx();
        validate(fetch_val(db, verify_t, "gamma") == "10", "First updater (T1) values retained");
        db.commit_tx(verify_t);
    }

    // --------------------------------------------------------------------
    print_header("Test 5: Garbage collection and historical pruning");
    // --------------------------------------------------------------------
    {
        std::size_t initial_vol = db.get_total_versions();
        std::size_t swept = db.cleanup_garbage();
        std::size_t final_vol = db.get_total_versions();

        std::cout << "  Version count: " << initial_vol << " -> " << final_vol
                  << "  (Swept records: " << swept << ")\n";

        validate(swept > 0, "Obsolete versions were successfully cleared");
        validate(initial_vol - swept == final_vol, "Version arithmetic holds post-GC");

        TransactionID final_check = db.start_tx();
        validate(fetch_val(db, final_check, "alpha") == "200", "alpha=200 intact after vacuum");
        validate(fetch_val(db, final_check, "gamma") == "10",  "gamma=10 intact after vacuum");
        db.commit_tx(final_check);
    }

    std::cout << "\n[SUCCESS] Engine diagnostics complete.\n";
    return EXIT_SUCCESS;
}