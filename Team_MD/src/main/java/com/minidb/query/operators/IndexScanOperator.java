package com.minidb.query.operators;

import com.minidb.index.BTreeIndex;
import com.minidb.query.Operator;
import com.minidb.storage.HeapFile;
import com.minidb.storage.Row;
import com.minidb.storage.RowId;

import java.util.Iterator;
import java.util.List;

public class IndexScanOperator implements Operator {
    private BTreeIndex index;
    private HeapFile heapFile;
    private int searchKey;
    private Iterator<RowId> resultIterator;

    private com.minidb.txn.TransactionManager txnManager;

    public IndexScanOperator(BTreeIndex index, HeapFile heapFile, int searchKey) {
        this(index, heapFile, searchKey, null);
    }

    public IndexScanOperator(BTreeIndex index, HeapFile heapFile, int searchKey, com.minidb.txn.TransactionManager txnManager) {
        this.index = index;
        this.heapFile = heapFile;
        this.searchKey = searchKey;
        this.txnManager = txnManager;
    }

    @Override
    public void open() throws Exception {
        List<RowId> results = index.search(searchKey);
        resultIterator = results.iterator();
    }

    @Override
    public Row next() throws Exception {
        if (resultIterator != null) {
            while (resultIterator.hasNext()) {
                RowId rid = resultIterator.next();
                Row row = heapFile.get(rid);
                if (row != null) {
                    row.setRowId(rid);
                    com.minidb.txn.Transaction txn = txnManager != null ? txnManager.getCurrent() : null;
                    if (com.minidb.txn.VisibilityRules.isVisible(row, txn, txnManager)) {
                        return row;
                    }
                }
            }
        }
        return null;
    }

    @Override
    public void close() throws Exception {
        resultIterator = null;
    }
}
