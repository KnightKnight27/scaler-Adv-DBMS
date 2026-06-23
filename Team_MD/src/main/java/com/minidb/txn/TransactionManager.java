package com.minidb.txn;

import com.minidb.recovery.LogRecord;
import com.minidb.recovery.WALManager;
import com.minidb.storage.RowId;
import java.io.IOException;
import java.util.Collections;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

public class TransactionManager {

    private final AtomicLong nextTxnId = new AtomicLong(1);
    private final Set<Long> activeTxns = Collections.newSetFromMap(new ConcurrentHashMap<>());
    private final Set<Long> committedTxns = Collections.newSetFromMap(new ConcurrentHashMap<>());
    
    // For First-Committer-Wins OCC: store write sets of committed txns.
    // Key: txnId, Value: set of rows modified
    private final ConcurrentHashMap<Long, Set<RowId>> historyWriteSets = new ConcurrentHashMap<>();

    private final ThreadLocal<Transaction> currentTxn = new ThreadLocal<>();
    
    private final WALManager walManager;

    public TransactionManager(WALManager walManager) {
        this.walManager = walManager;
    }

    public Transaction begin() {
        long txnId = nextTxnId.getAndIncrement();
        long maxCommitted = 0;
        for (Long id : committedTxns) {
            if (id > maxCommitted) maxCommitted = id;
        }
        Transaction txn = new Transaction(txnId, maxCommitted);
        activeTxns.add(txnId);
        currentTxn.set(txn);
        return txn;
    }

    public void commit(Transaction txn) {
        // Step 1: First-Committer-Wins OCC check
        for (Long committedId : committedTxns) {
            if (committedId > txn.getSnapshotId()) {
                Set<RowId> overlap = historyWriteSets.get(committedId);
                if (overlap != null) {
                    for (RowId rowId : txn.getWriteSet()) {
                        if (overlap.contains(rowId)) {
                            abort(txn);
                            throw new RuntimeException("MVCC Write Conflict: Transaction " + txn.getId() + " aborted due to concurrent update on Row " + rowId);
                        }
                    }
                }
            }
        }

        // Write COMMIT record to WAL before completing commit
        if (walManager != null) {
            try {
                LogRecord record = new LogRecord(txn.getId(), -1, null, null, LogRecord.Type.COMMIT);
                walManager.appendLogRecord(record);
            } catch (IOException e) {
                throw new RuntimeException("Failed to write COMMIT log record", e);
            }
        }

        // Step 2: Commit
        activeTxns.remove(txn.getId());
        committedTxns.add(txn.getId());
        historyWriteSets.put(txn.getId(), txn.getWriteSet());
        currentTxn.remove();
    }

    public void abort(Transaction txn) {
        if (walManager != null) {
            try {
                LogRecord record = new LogRecord(txn.getId(), -1, null, null, LogRecord.Type.ABORT);
                walManager.appendLogRecord(record);
            } catch (IOException e) {
                throw new RuntimeException("Failed to write ABORT log record", e);
            }
        }
        
        activeTxns.remove(txn.getId());
        currentTxn.remove();
    }

    public boolean isCommitted(long txnId) {
        return committedTxns.contains(txnId);
    }

    public Transaction getCurrent() {
        return currentTxn.get();
    }
}
