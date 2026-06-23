package com.minidb.benchmark;

import com.minidb.index.BTreeIndex;
import com.minidb.query.operators.IndexScanOperator;
import com.minidb.query.operators.SeqScanOperator;
import com.minidb.query.operators.FilterOperator;
import com.minidb.storage.*;

import java.io.File;
import java.util.Arrays;
import java.util.List;

public class BenchmarkRunner {
    private static final String TABLE_FILE = "bench_table.db";
    private static final String INDEX_FILE = "bench_index.db";

    public static void main(String[] args) throws Exception {
        new File(TABLE_FILE).delete();
        new File(INDEX_FILE).delete();

        PageManager pm = new PageManager(TABLE_FILE);
        List<ColumnType> schema = Arrays.asList(ColumnType.INT, ColumnType.VARCHAR);
        HeapFile heapFile = new HeapFile(new BufferPool(pm, 1024), pm, schema);
        BTreeIndex index = new BTreeIndex(INDEX_FILE);

        System.out.println("Inserting 10,000 rows...");
        for (int i = 0; i < 10000; i++) {
            RowId rid = heapFile.insert(new Row(new Object[]{i, "Data" + i}));
            index.insert(i, rid);
        }

        int searchKey = 9999;

        // SeqScan Benchmark
        long startSeq = System.nanoTime();
        FilterOperator.Predicate pred = row -> (int) row.getValue(0) == searchKey;
        FilterOperator seqOp = new FilterOperator(new SeqScanOperator(heapFile), pred);
        seqOp.open();
        Row seqResult = seqOp.next();
        seqOp.close();
        long seqTime = (System.nanoTime() - startSeq) / 1000000; // ms
        System.out.println("SeqScan time: " + seqTime + "ms");

        // IndexScan Benchmark
        long startIndex = System.nanoTime();
        IndexScanOperator idxOp = new IndexScanOperator(index, heapFile, searchKey);
        idxOp.open();
        Row idxResult = idxOp.next();
        idxOp.close();
        long idxTime = (System.nanoTime() - startIndex) / 1000000; // ms
        System.out.println("IndexScan time: " + idxTime + "ms");

        pm.close();
        index.close();
        new File(TABLE_FILE).delete();
        new File(INDEX_FILE).delete();
    }
}
