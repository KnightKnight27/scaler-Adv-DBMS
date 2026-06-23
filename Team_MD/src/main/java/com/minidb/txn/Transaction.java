package com.minidb.txn;

import com.minidb.storage.RowId;
import java.util.HashSet;
import java.util.Set;

public class Transaction {
    private final long txnId;
    private final long snapshotId;
    private final Set<RowId> writeSet;

    public Transaction(long txnId, long snapshotId) {
        this.txnId = txnId;
        this.snapshotId = snapshotId;
        this.writeSet = new HashSet<>();
    }

    public long getId() { return txnId; }
    public long getSnapshotId() { return snapshotId; }
    public Set<RowId> getWriteSet() { return writeSet; }

    public void addWrite(RowId rowId) {
        writeSet.add(rowId);
    }
}
