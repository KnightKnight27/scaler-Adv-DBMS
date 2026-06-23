// main.cpp — ADBMS Lab 7 test runner / 24bcs10632 Patel Jash
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

using namespace database_core;

namespace {

void show_title(const std::string& heading) {
    std::cout << "\n=== " << heading << " ===\n";
}

void assert_check(bool is_valid, const std::string& message) {
    std::cout << (is_valid ? "  [PASS] " : "  [FAIL] ") << message << "\n";
    if (!is_valid) std::exit(EXIT_FAILURE);
}

// Helper to fetch data safely and fallback to <empty>
std::string retrieve_val(TxManager& mgr, TxID t_id, const std::string& key) {
    std::string val;
    EngineStatus st = mgr.read_key(t_id, key, val);
    return st == EngineStatus::Ok ? val : "<empty>";
}

}  // anonymous namespace

int main() {
    TxManager db_mgr;

    // --------------------------------------------------------------------
    show_title("Test 1: MVCC snapshot consistency");
    // --------------------------------------------------------------------
    {
        TxID tx_init = db_mgr.begin_transaction();
        db_mgr.write_key(tx_init, "var_a", "100");
        assert_check(db_mgr.commit(tx_init) == EngineStatus::Ok, "Initial record 'var_a'=100 committed");

        TxID tx_read = db_mgr.begin_transaction();
        std::cout << "  TX-" << tx_read << " fetches var_a = " << retrieve_val(db_mgr, tx_read, "var_a") << "\n";

        TxID tx_write = db_mgr.begin_transaction();
        db_mgr.write_key(tx_write, "var_a", "200");
        assert_check(db_mgr.commit(tx_write) == EngineStatus::Ok, "Concurrent writer updates var_a=200");

        std::string res = retrieve_val(db_mgr, tx_read, "var_a");
        std::cout << "  TX-" << tx_read << " fetches var_a again = " << res << " (isolated view)\n";
        assert_check(res == "100", "Reader still observes historical snapshot var_a=100");

        TxID tx_fresh = db_mgr.begin_transaction();
        assert_check(retrieve_val(db_mgr, tx_fresh, "var_a") == "200", "Fresh transaction sees var_a=200");

        db_mgr.commit(tx_read);
        db_mgr.commit(tx_fresh);
    }

    // --------------------------------------------------------------------
    show_title("Test 2: Strict 2PL concurrency locks");
    // --------------------------------------------------------------------
    {
        TxID ta = db_mgr.begin_transaction();
        TxID tb = db_mgr.begin_transaction();

        assert_check(db_mgr.write_key(ta, "var_b", "1") == EngineStatus::Ok, "TX-A acquires exclusive lock on var_b");
        assert_check(db_mgr.write_key(tb, "var_b", "2") == EngineStatus::Blocked, "TX-B update gets BLOCKED status");

        db_mgr.abort_transaction(ta);
        assert_check(db_mgr.get_transaction_status(ta) == TxState::Terminated, "TX-A rolled back, releasing lock");
        assert_check(db_mgr.write_key(tb, "var_b", "2") == EngineStatus::Ok, "TX-B successfully acquires var_b lock");
        assert_check(db_mgr.commit(tb) == EngineStatus::Ok, "TX-B commits cleanly");

        TxID t_check = db_mgr.begin_transaction();
        assert_check(retrieve_val(db_mgr, t_check, "var_b") == "2", "var_b successfully persisted as 2");
        db_mgr.commit(t_check);
    }

    // --------------------------------------------------------------------
    show_title("Test 3: Cyclic deadlock detection & resolution");
    // --------------------------------------------------------------------
    {
        TxID tx_1 = db_mgr.begin_transaction();
        TxID tx_2 = db_mgr.begin_transaction();

        assert_check(db_mgr.write_key(tx_1, "item_x", "V1") == EngineStatus::Ok, "TX-1 locks item_x");
        assert_check(db_mgr.write_key(tx_2, "item_y", "V2") == EngineStatus::Ok, "TX-2 locks item_y");

        assert_check(db_mgr.write_key(tx_1, "item_y", "V3") == EngineStatus::Blocked, "TX-1 waits on TX-2 for item_y");

        EngineStatus outcome = db_mgr.write_key(tx_2, "item_x", "V4");
        std::cout << "  TX-2 requests item_x -> " << engine_status_to_string(outcome)
                  << " | Aborted Target = TX-" << db_mgr.get_last_killed() << "\n";

        assert_check(outcome == EngineStatus::Aborted, "TX-2 forcefully aborted due to deadlock cycle");
        assert_check(db_mgr.get_last_killed() == tx_2, "Target was correctly identified as the newer transaction");
        assert_check(db_mgr.get_transaction_status(tx_2) == TxState::Terminated, "TX-2 is in Terminated state");

        assert_check(db_mgr.write_key(tx_1, "item_y", "V3") == EngineStatus::Ok, "TX-1 assumes lock over item_y after TX-2 dies");
        assert_check(db_mgr.commit(tx_1) == EngineStatus::Ok, "TX-1 commits item_x=V1, item_y=V3");

        TxID t_check = db_mgr.begin_transaction();
        assert_check(retrieve_val(db_mgr, t_check, "item_x") == "V1" && retrieve_val(db_mgr, t_check, "item_y") == "V3", "Deadlock resolution values persisted");
        db_mgr.commit(t_check);
    }

    // --------------------------------------------------------------------
    show_title("Test 4: First-updater-wins constraint");
    // --------------------------------------------------------------------
    {
        TxID t_base = db_mgr.begin_transaction();
        db_mgr.write_key(t_base, "var_c", "0");
        db_mgr.commit(t_base);

        TxID t1 = db_mgr.begin_transaction();
        TxID t2 = db_mgr.begin_transaction();

        assert_check(db_mgr.write_key(t1, "var_c", "10") == EngineStatus::Ok, "T1 updates var_c=10");
        assert_check(db_mgr.write_key(t2, "var_c", "20") == EngineStatus::Blocked, "T2 waits behind T1");

        assert_check(db_mgr.commit(t1) == EngineStatus::Ok, "T1 commits var_c=10");

        assert_check(db_mgr.write_key(t2, "var_c", "20") == EngineStatus::Ok, "T2 lock granted post T1 commit");
        EngineStatus commit_st = db_mgr.commit(t2);
        std::cout << "  T2 commit attempt -> " << engine_status_to_string(commit_st) << "\n";

        assert_check(commit_st == EngineStatus::SerializationError, "T2 block rejected due to concurrent mutation conflict");

        TxID t_verify = db_mgr.begin_transaction();
        assert_check(retrieve_val(db_mgr, t_verify, "var_c") == "10", "First updater (T1) values retained");
        db_mgr.commit(t_verify);
    }

    // --------------------------------------------------------------------
    show_title("Test 5: Garbage collection and historical pruning");
    // --------------------------------------------------------------------
    {
        std::size_t count_start = db_mgr.count_all_versions();
        std::size_t swept_items = db_mgr.run_vacuum();
        std::size_t count_end = db_mgr.count_all_versions();

        std::cout << "  Version count: " << count_start << " -> " << count_end
                  << "  (Swept records: " << swept_items << ")\n";

        assert_check(swept_items > 0, "Obsolete versions were successfully cleared");
        assert_check(count_start - swept_items == count_end, "Version arithmetic holds post-GC");

        TxID final_t = db_mgr.begin_transaction();
        assert_check(retrieve_val(db_mgr, final_t, "var_a") == "200", "var_a=200 intact after vacuum");
        assert_check(retrieve_val(db_mgr, final_t, "var_c") == "10",  "var_c=10 intact after vacuum");
        db_mgr.commit(final_t);
    }

    std::cout << "\n[SUCCESS] Engine diagnostics complete.\n";
    return EXIT_SUCCESS;
}