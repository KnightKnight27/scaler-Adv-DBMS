package com.minidb.query.operators;

import com.minidb.query.Operator;
import com.minidb.storage.HeapFile;
import com.minidb.storage.Row;

import java.util.Iterator;
import java.util.List;

public class InsertOperator implements Operator {
    private HeapFile heapFile;
    private Iterator<Row> rowsToInsert;
    private int insertCount = 0;
    private boolean executed = false;

    private com.minidb.txn.TransactionManager txnManager;

    public InsertOperator(HeapFile heapFile, List<Row> rowsToInsert) {
        this(heapFile, rowsToInsert, null);
    }

    public InsertOperator(HeapFile heapFile, List<Row> rowsToInsert, com.minidb.txn.TransactionManager txnManager) {
        this.heapFile = heapFile;
        this.rowsToInsert = rowsToInsert.iterator();
        this.txnManager = txnManager;
    }

    @Override
    public void open() throws Exception {
        insertCount = 0;
        executed = false;
    }

    @Override
    public Row next() throws Exception {
        if (executed) return null;

        while (rowsToInsert.hasNext()) {
            Row row = rowsToInsert.next();
            com.minidb.txn.Transaction txn = txnManager != null ? txnManager.getCurrent() : null;
            if (txn != null) {
                row.setXmin(txn.getId());
                row.setXmax(0);
            }
            com.minidb.storage.RowId id = heapFile.insert(row);
            if (txn != null) {
                txn.addWrite(id);
            }
            insertCount++;
        }
        executed = true;
        return new Row(new Object[]{insertCount});
    }

    @Override
    public void close() throws Exception {
    }
}
