package com.minidb.query;

import com.minidb.index.BTreeIndex;
import com.minidb.query.operators.*;
import com.minidb.storage.HeapFile;
import com.minidb.txn.TransactionManager;

import java.util.List;

public class QueryOptimizer {

    /**
     * Estimates selectivity of a predicate.
     * Equality filter on an indexed column has high selectivity (returns fewer rows, e.g. 0.05).
     * Otherwise, assumes lower selectivity (e.g. 1.0) requiring a table scan.
     */
    public double estimateSelectivity(BTreeIndex index, FilterOperator.Predicate predicate, int searchKey) {
        if (index != null && searchKey != -1) {
            // High selectivity (e.g., 5% of rows or a single row) for equality on indexed column
            return 0.05; 
        }
        // Low selectivity (all rows) for no index or non-equality predicates
        return 1.0; 
    }

    public Operator optimize(String sql, HeapFile heapFile, BTreeIndex index, FilterOperator.Predicate predicate, int searchKey, List<Integer> projections) {
        return optimize(sql, heapFile, index, predicate, searchKey, projections, null);
    }

    public Operator optimize(String sql, HeapFile heapFile, BTreeIndex index, FilterOperator.Predicate predicate, int searchKey, List<Integer> projections, TransactionManager txnManager) {
        Operator op;
        double selectivity = estimateSelectivity(index, predicate, searchKey);
        
        if (selectivity < 0.1 && index != null && searchKey != -1) {
            op = new IndexScanOperator(index, heapFile, searchKey, txnManager);
        } else {
            op = new SeqScanOperator(heapFile, txnManager);
            if (predicate != null) {
                op = new FilterOperator(op, predicate);
            }
        }
        
        if (projections != null && !projections.isEmpty()) {
            op = new ProjectOperator(op, projections);
        }
        
        return op;
    }

    /**
     * Join order selection for 2-table joins.
     * Queries the HeapFile for row counts and picks the smaller table to drive the outer loop.
     */
    public Operator optimizeJoin(HeapFile leftTable, HeapFile rightTable, int leftJoinCol, int rightJoinCol, TransactionManager txnManager) {
        long leftCount = 0;
        long rightCount = 0;
        
        try {
            // Querying row counts via scan() for simplicity in this system
            leftCount = leftTable.scan().size();
            rightCount = rightTable.scan().size();
        } catch (Exception e) {
            // Ignore scan errors, default to 0
        }
        
        Operator leftScan = new SeqScanOperator(leftTable, txnManager);
        Operator rightScan = new SeqScanOperator(rightTable, txnManager);
        
        if (leftCount <= rightCount) {
            // Left is smaller, drives the outer loop
            return new JoinOperator(leftScan, rightScan, leftJoinCol, rightJoinCol);
        } else {
            // Right is smaller, drives the outer loop. 
            // Note that JoinOperator expects (outer, inner, outerCol, innerCol).
            return new JoinOperator(rightScan, leftScan, rightJoinCol, leftJoinCol);
        }
    }
}
