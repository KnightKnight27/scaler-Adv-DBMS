package com.minidb.query.operators;

import com.minidb.query.Operator;
import com.minidb.storage.HeapFile;
import com.minidb.storage.Row;

import java.util.Iterator;
import java.util.List;

public class SeqScanOperator implements Operator {
    private HeapFile heapFile;
    private Iterator<Row> rowIterator;

    private com.minidb.txn.TransactionManager txnManager;

    public SeqScanOperator(HeapFile heapFile) {
        this(heapFile, null);
    }

    public SeqScanOperator(HeapFile heapFile, com.minidb.txn.TransactionManager txnManager) {
        this.heapFile = heapFile;
        this.txnManager = txnManager;
    }

    @Override
    public void open() throws Exception {
        rowIterator = heapFile.iterator();
    }

    @Override
    public Row next() throws Exception {
        if (rowIterator != null) {
            while (rowIterator.hasNext()) {
                Row row = rowIterator.next();
                com.minidb.txn.Transaction txn = txnManager != null ? txnManager.getCurrent() : null;
                if (com.minidb.txn.VisibilityRules.isVisible(row, txn, txnManager)) {
                    return row;
                }
            }
        }
        return null;
    }

    @Override
    public void close() throws Exception {
        rowIterator = null;
    }
}
