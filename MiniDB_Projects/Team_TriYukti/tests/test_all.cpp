#include "storage/buffer_pool.h"
#include "index/bplus_tree.h"
#include "transaction/transaction.h"
#include "transaction/lock_manager.h"
#include "recovery/log_manager.h"
#include "recovery/recovery.h"
#include "mvcc/mvcc_manager.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <cstdio>

using namespace minidb;

void test_storage() {
    std::remove("test.db");
    PageManager pm("test.db");
    BufferPool bp(5, &pm);
    
    page_id_t pid;
    Page *p = bp.NewPage(&pid);
    assert(p != nullptr);
    
    Tuple t;
    t.data_ = {'a', 'b', 'c'};
    RecordId rid;
    assert(p->InsertTuple(t, &rid));
    
    bp.UnpinPage(pid, true);
    bp.FlushAllPages();
    std::cout << "Storage test passed.\n";
}

void test_bplus_tree() {
    std::remove("test_idx.db");
    PageManager pm("test_idx.db");
    BufferPool bp(10, &pm);
    BPlusTree tree(&bp);
    
    RecordId r1{1, 1};
    tree.Insert(10, r1);
    
    RecordId res;
    assert(tree.Search(10, &res));
    assert(res.page_id == 1 && res.slot_id == 1);
    
    std::cout << "B+ Tree test passed.\n";
}

void test_mvcc() {
    std::remove("test_mvcc.db");
    PageManager pm("test_mvcc.db");
    BufferPool bp(5, &pm);
    MVCCManager mvcc(&bp);
    Transaction txn1(1);
    txn1.SetSnapshotTimestamp(1);
    
    page_id_t pid;
    Page *p = bp.NewPage(&pid);
    
    Tuple t1;
    t1.data_.resize(9);
    t1.SetCreatedBy(1);
    t1.SetDeletedBy(-1);
    t1.data_[8] = 'A';
    RecordId rid;
    p->InsertTuple(t1, &rid);
    bp.UnpinPage(pid, true);
    
    // InsertVersion is now physical no-op in manager
    mvcc.InsertVersion(rid, t1, &txn1);
    mvcc.RecordCommit(txn1.GetTransactionId(), 1);
    
    Transaction txn2(2);
    txn2.SetSnapshotTimestamp(2);
    
    Tuple res;
    assert(mvcc.ReadVisibleVersion(rid, &txn1, &res));
    assert(res.data_[8] == 'A');
    
    Transaction txn3(3);
    txn3.SetSnapshotTimestamp(3);
    mvcc.DeleteVersion(rid, &txn3);
    mvcc.RecordCommit(txn3.GetTransactionId(), 3);
    
    assert(mvcc.ReadVisibleVersion(rid, &txn2, &res));
    
    Transaction txn4(4);
    txn4.SetSnapshotTimestamp(4);
    assert(!mvcc.ReadVisibleVersion(rid, &txn4, &res));
    
    std::cout << "MVCC test passed.\n";
}

void test_recovery() {
    std::remove("test_rec.db");
    std::remove("test_rec.log");
    
    page_id_t target_pid;
    RecordId target_rid;
    Tuple t_before; t_before.data_ = {'O', 'L', 'D'};
    Tuple t_after; t_after.data_ = {'N', 'E', 'W'};
    
    {
        PageManager pm("test_rec.db");
        BufferPool bp(5, &pm);
        LogManager logm("test_rec.log");
        
        Page *p = bp.NewPage(&target_pid);
        p->InsertTuple(t_before, &target_rid); // simulate pre-crash state written to disk
        
        bp.UnpinPage(target_pid, true);
        bp.FlushAllPages(); 
        
        LogRecord r;
        r.type = LogRecordType::UPDATE;
        r.txn_id = 100;
        r.lsn = 5;
        r.rid = target_rid;
        r.before_image = t_before;
        r.after_image = t_after;
        logm.AppendLogRecord(r);
        
        LogRecord c;
        c.type = LogRecordType::COMMIT;
        c.txn_id = 100;
        c.lsn = 6;
        logm.AppendLogRecord(c);
        logm.Flush();
    }
    
    {
        PageManager pm("test_rec.db");
        BufferPool bp(5, &pm);
        LogManager logm("test_rec.log");
        MVCCManager mvcc(&bp);
        RecoveryManager rm(&logm, &bp, &mvcc);
        
        rm.Recover();
        
        Page *p = bp.FetchPage(target_pid);
        assert(p != nullptr);
        Tuple read_t;
        assert(p->GetTuple(target_rid.slot_id, &read_t));
        assert(read_t.data_.size() == 3 && read_t.data_[0] == 'N' && read_t.data_[1] == 'E' && read_t.data_[2] == 'W');
        bp.UnpinPage(target_pid, false);
    }
    std::cout << "Recovery test passed.\n";
}

void test_deadlock() {
    LockManager lm;
    
    Transaction txn1(1);
    Transaction txn2(2);
    
    lm.AddTransaction(&txn1);
    lm.AddTransaction(&txn2);
    
    lm.LockExclusive(&txn1, "A");
    lm.LockExclusive(&txn2, "B");
    
    std::thread th1([&]() {
        if (lm.LockExclusive(&txn1, "B")) {
            lm.Unlock(&txn1, "B");
        }
        lm.Unlock(&txn1, "A");
    });
    
    std::thread th2([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (!lm.LockExclusive(&txn2, "A")) {
            lm.Unlock(&txn2, "B");
        }
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    
    int elapsed = 1500;
    while (txn2.GetState() != TransactionState::ABORTED && txn1.GetState() != TransactionState::ABORTED && elapsed < 3000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        elapsed += 100;
    }
    
    if (txn2.GetState() != TransactionState::ABORTED && txn1.GetState() != TransactionState::ABORTED) {
        std::cerr << "Deadlock test failed: neither transaction aborted within 3 seconds.\n";
        exit(1);
    }
    
    th1.join();
    th2.join();
    
    assert(txn2.GetState() == TransactionState::ABORTED);
    assert(txn1.GetState() == TransactionState::ACTIVE);
    
    std::cout << "Deadlock detection test passed.\n";
}

int main() {
    test_storage();
    test_bplus_tree();
    test_mvcc();
    test_recovery();
    test_deadlock();
    std::cout << "All tests passed successfully.\n";
    return 0;
}
