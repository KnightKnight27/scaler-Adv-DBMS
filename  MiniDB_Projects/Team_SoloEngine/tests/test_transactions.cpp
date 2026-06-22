#include "storage.h"
#include "buffer_pool.h"
#include "table.h"
#include "execution.h"
#include "transaction.h"
#include "recovery.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <set>
#include <thread>

static const std::string DB_FILE  = "/tmp/test_txn_engine.db";
static const std::string WAL_FILE = "/tmp/test_solo_wal.log";
static void cleanup_db()  { std::filesystem::remove(DB_FILE); }
static void cleanup_wal() { std::filesystem::remove(WAL_FILE); }

// ─── Test 1: Strict 2PL — timeout/abort under contention ─────────────────────
//
// Thread 1 acquires an EXCLUSIVE lock on RID {0, 0} and holds it for 100 ms.
// Thread 2 (signalled to start only after Thread 1 holds the lock) tries to
// acquire a SHARED lock on the same RID.  It must time-out at 50 ms and throw
// TransactionAbortException.

static void test_lock_timeout() {
    LockManager lm;

    Transaction txn1(1);
    Transaction txn2(2);
    RID rid{0, 0};

    // Promise/future pair ensures Thread 2 never races to grab the lock first.
    std::promise<void> lock_ready;
    std::future<void>  lock_ready_fut = lock_ready.get_future();

    bool t2_aborted = false;

    std::thread t1([&]() {
        lm.AcquireExclusiveLock(&txn1, rid);
        lock_ready.set_value();                             // signal: lock is held
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        txn1.SetState(TxnState::COMMITTED);
        lm.ReleaseLocks(&txn1);
    });

    std::thread t2([&]() {
        lock_ready_fut.wait();                              // wait until T1 holds the lock
        try {
            lm.AcquireSharedLock(&txn2, rid);
            // Should NOT reach here — the lock is exclusively held for 100 ms
            // but our timeout is 50 ms.
        } catch (const TransactionAbortException &) {
            t2_aborted = true;
        }
    });

    t1.join();
    t2.join();

    assert(t2_aborted);
    assert(txn2.GetState() == TxnState::ABORTED);
    std::cout << "[PASS] test_lock_timeout\n";
}

// ─── Test 2: compatible shared locks on the same RID coexist ─────────────────

static void test_shared_lock_compat() {
    LockManager lm;
    Transaction txn1(10);
    Transaction txn2(11);
    RID rid{1, 0};

    // Both shared — should both be granted without waiting.
    lm.AcquireSharedLock(&txn1, rid);
    lm.AcquireSharedLock(&txn2, rid);

    assert(txn1.GetState() == TxnState::GROWING);
    assert(txn2.GetState() == TxnState::GROWING);

    txn1.SetState(TxnState::COMMITTED); lm.ReleaseLocks(&txn1);
    txn2.SetState(TxnState::COMMITTED); lm.ReleaseLocks(&txn2);

    std::cout << "[PASS] test_shared_lock_compat\n";
}

// ─── Test 3: WAL write + crash simulation + Redo recovery ────────────────────
//
// Phase A: write BEGIN + 5 INSERT + COMMIT records to WAL, then "crash"
//          (destroy BPM without flushing heap data to the recovery DB).
// Phase B: spin up a fresh BPM and TableHeap, run RecoveryManager::Redo(),
//          and assert that all 5 tuples are present with correct values.

static void test_wal_recovery() {
    cleanup_wal();
    cleanup_db();

    // ── Phase A: write the WAL (no heap involved) ─────────────────────────
    {
        LogManager log(WAL_FILE);

        constexpr txn_id_t TXN_ID = 42;

        // BEGIN
        LogRecord begin_rec;
        begin_rec.txn_id = TXN_ID;
        begin_rec.type   = LogType::BEGIN;
        log.AppendRecord(begin_rec);

        // 5 INSERT records (RID fields are metadata — Redo uses only tuple data)
        for (int i = 0; i < 5; ++i) {
            LogRecord ins;
            ins.txn_id   = TXN_ID;
            ins.type     = LogType::INSERT;
            ins.page_id  = 0;          // placeholder
            ins.slot_num = i;          // placeholder
            ins.id       = static_cast<int64_t>(i);
            ins.val1     = static_cast<int64_t>(i * 10);
            ins.val2     = static_cast<int64_t>(i * 100);
            log.AppendRecord(ins);
        }

        // COMMIT — makes this txn eligible for Redo
        LogRecord commit_rec;
        commit_rec.txn_id = TXN_ID;
        commit_rec.type   = LogType::COMMIT;
        log.AppendRecord(commit_rec);
        log.Flush();
    }
    // LogManager destructor closes the file — WAL is on disk.

    // ── Also write a record for an UNCOMMITTED txn to verify it is not redone
    {
        LogManager log(WAL_FILE);
        LogRecord orphan;
        orphan.txn_id   = 99;
        orphan.type     = LogType::INSERT;
        orphan.id       = 999;
        orphan.val1     = 999;
        orphan.val2     = 999;
        log.AppendRecord(orphan);
        // No COMMIT for txn 99 — must NOT appear after Redo.
        log.Flush();
    }

    // ── Phase B: recovery into a fresh heap ───────────────────────────────
    {
        DiskManager dm(DB_FILE);
        BufferPoolManager bpm(64, &dm);
        TableHeap heap(&bpm);

        RecoveryManager rm(WAL_FILE, &heap);
        rm.Redo();

        assert(bpm.AllUnpinned());

        // Collect recovered tuples
        SeqScanExecutor scan(&heap);
        scan.Init();
        std::set<int64_t> found_ids;
        Tuple t;
        while (scan.Next(&t)) {
            assert(t.val1 == t.id * 10);
            assert(t.val2 == t.id * 100);
            found_ids.insert(t.id);
        }

        assert(static_cast<int>(found_ids.size()) == 5);
        for (int64_t i = 0; i < 5; ++i) assert(found_ids.count(i));

        assert(bpm.AllUnpinned());
    }

    cleanup_wal();
    cleanup_db();
    std::cout << "[PASS] test_wal_recovery\n";
}

// ─── Test 4: partial WAL (only ABORT, no COMMIT) — nothing redone ─────────────

static void test_wal_abort_not_redone() {
    cleanup_wal();
    cleanup_db();

    {
        LogManager log(WAL_FILE);
        LogRecord ins;
        ins.txn_id = 7;
        ins.type   = LogType::INSERT;
        ins.id     = 77;
        ins.val1   = 770;
        ins.val2   = 7700;
        log.AppendRecord(ins);

        LogRecord abort_rec;
        abort_rec.txn_id = 7;
        abort_rec.type   = LogType::ABORT;
        log.AppendRecord(abort_rec);
        log.Flush();
    }

    {
        DiskManager dm(DB_FILE);
        BufferPoolManager bpm(64, &dm);
        TableHeap heap(&bpm);

        RecoveryManager rm(WAL_FILE, &heap);
        rm.Redo();

        SeqScanExecutor scan(&heap);
        scan.Init();
        int count = 0;
        Tuple t;
        while (scan.Next(&t)) ++count;
        assert(count == 0);
        assert(bpm.AllUnpinned());
    }

    cleanup_wal();
    cleanup_db();
    std::cout << "[PASS] test_wal_abort_not_redone\n";
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Transaction & Recovery Tests ===\n";
    test_lock_timeout();
    test_shared_lock_compat();
    test_wal_recovery();
    test_wal_abort_not_redone();
    std::cout << "\nAll transaction & recovery tests passed.\n";
    return 0;
}
