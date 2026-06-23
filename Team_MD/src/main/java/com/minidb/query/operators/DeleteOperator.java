package com.minidb.query.operators;

import com.minidb.query.Operator;
import com.minidb.storage.HeapFile;
import com.minidb.storage.Row;
import com.minidb.storage.RowId;

import java.util.Map;
import java.util.AbstractMap;

public class DeleteOperator implements Operator {
    private Operator child;
    private HeapFile heapFile;
    private int deleteCount = 0;
    private boolean executed = false;

    private com.minidb.txn.TransactionManager txnManager;

    public DeleteOperator(HeapFile heapFile, Operator child) {
        this(heapFile, child, null);
    }

    public DeleteOperator(HeapFile heapFile, Operator child, com.minidb.txn.TransactionManager txnManager) {
        this.heapFile = heapFile;
        this.child = child;
        this.txnManager = txnManager;
    }

    @Override
    public void open() throws Exception {
        child.open();
        deleteCount = 0;
        executed = false;
    }

    @Override
    public Row next() throws Exception {
        if (executed) return null;

        Row row;
        while ((row = child.next()) != null) {
            com.minidb.txn.Transaction txn = txnManager != null ? txnManager.getCurrent() : null;
            if (row.getRowId() != null) {
                if (txn != null) {
                    row.setXmax(txn.getId());
                    heapFile.update(row.getRowId(), row);
                    txn.addWrite(row.getRowId());
                } else {
                    heapFile.delete(row.getRowId()); // Fallback for non-transactional phase 2 tests
                }
            }
            deleteCount++;
        }
        executed = true;
        return new Row(new Object[]{deleteCount});
    }

    @Override
    public void close() throws Exception {
        child.close();
    }
}
