package com.minidb.txn;

import com.minidb.storage.Row;

public class VisibilityRules {

    public static boolean isVisible(Row row, Transaction t, TransactionManager txnManager) {
        if (t == null) return true; // If no active transaction, everything is visible (e.g., Phase 2 tests)

        // XMIN check: The transaction that created this row must be committed AND must have committed
        // before our transaction started (xmin <= t.snapshotId), OR it must be our own transaction.
        boolean xminValid = (row.getXmin() == t.getId()) ||
                (txnManager.isCommitted(row.getXmin()) && row.getXmin() <= t.getSnapshotId());

        // XMAX check: The transaction that deleted this row must NOT be committed, OR it must have 
        // committed AFTER our transaction started (xmax > t.snapshotId). If xmax == 0, it wasn't deleted.
        // If xmax == t.getId(), we deleted it ourselves so it's not visible.
        boolean xmaxValid = (row.getXmax() == 0) ||
                (row.getXmax() != t.getId() && (!txnManager.isCommitted(row.getXmax()) || row.getXmax() > t.getSnapshotId()));

        return xminValid && xmaxValid;
    }
}
