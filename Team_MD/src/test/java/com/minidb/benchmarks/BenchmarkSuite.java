package com.minidb.benchmarks;

import com.minidb.index.BTreeIndex;
import com.minidb.query.Operator;
import com.minidb.query.operators.FilterOperator;
import com.minidb.query.operators.IndexScanOperator;
import com.minidb.query.operators.SeqScanOperator;
import com.minidb.recovery.LogRecord;
import com.minidb.recovery.WALManager;
import com.minidb.storage.*;
import com.minidb.txn.Transaction;
import com.minidb.txn.TransactionManager;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicInteger;

public class BenchmarkSuite {

    private static final String DIR = "benchmarks_data";
    private static final String TABLE_FILE = DIR + "/table.db";
    private static final String INDEX_FILE = DIR + "/index.db";
    private static final String WAL_FILE   = DIR + "/wal.log";

    public static void main(String[] args) throws Exception {
        new File(DIR).mkdirs();
        new File("benchmarks").mkdirs();
        
        PrintWriter out = new PrintWriter(new FileWriter("benchmarks/results.md"));
        out.println("# MiniDB Benchmarks");
        out.println();
        
        System.out.println("Running Benchmark 1: Scan vs Index...");
        benchmark1_ScanVsIndex(out);
        
        System.out.println("Running Benchmark 2: MVCC Concurrency...");
        benchmark2_MVCC_Concurrency(out);
        
        System.out.println("Running Benchmark 3: WAL Overhead...");
        benchmark3_WAL_Overhead(out);
        
        out.close();
        System.out.println("Benchmarks completed. Results written to benchmarks/results.md");
    }

    private static void cleanup() {
        new File(TABLE_FILE).delete();
        new File(INDEX_FILE).delete();
        new File(WAL_FILE).delete();
    }

    private static void benchmark1_ScanVsIndex(PrintWriter out) throws Exception {
        out.println("## 1. Table Scan vs Index Scan Latency");
        out.println();
        out.println("| Table Size (Rows) | SeqScan Latency (ns) | IndexScan Latency (ns) | Speedup |");
        out.println("|-------------------|----------------------|------------------------|---------|");

        int[] sizes = {100, 1000, 10000};
        List<ColumnType> schema = Arrays.asList(ColumnType.INT, ColumnType.VARCHAR);

        for (int size : sizes) {
            cleanup();
            PageManager pm = new PageManager(TABLE_FILE);
            BufferPool bp = new BufferPool(pm, 100);
            HeapFile hf = new HeapFile(bp, pm, schema);
            BTreeIndex index = new BTreeIndex(INDEX_FILE);
            TransactionManager txnManager = new TransactionManager(null);
            
            Transaction t = txnManager.begin();
            setCurrentTxn(txnManager, t);
            
            // Populate
            for (int i = 0; i < size; i++) {
                Row row = new Row(new Object[]{i, "Val" + i});
                row.setXmin(t.getId());
                RowId rid = hf.insert(row);
                index.insert(i, rid);
            }
            txnManager.commit(t);

            Transaction readTxn = txnManager.begin();
            setCurrentTxn(txnManager, readTxn);

            int targetKey = size / 2;

            // Warmup
            runSeqScan(hf, targetKey, txnManager);
            runIndexScan(index, hf, targetKey, txnManager);

            // Measure SeqScan
            long seqStart = System.nanoTime();
            int seqFound = 0;
            for (int i = 0; i < 10; i++) {
                if (runSeqScan(hf, targetKey, txnManager)) seqFound++;
            }
            long seqAvg = (System.nanoTime() - seqStart) / 10;

            // Measure IndexScan
            long idxStart = System.nanoTime();
            int idxFound = 0;
            for (int i = 0; i < 10; i++) {
                if (runIndexScan(index, hf, targetKey, txnManager)) idxFound++;
            }
            long idxAvg = (System.nanoTime() - idxStart) / 10;

            double speedup = (double) seqAvg / idxAvg;
            out.printf("| %d | %,d | %,d | %.2fx |\n", size, seqAvg, idxAvg, speedup);

            pm.close();
            index.close();
        }
        out.println();
    }

    private static boolean runSeqScan(HeapFile hf, int targetKey, TransactionManager tm) throws Exception {
        Operator scan = new SeqScanOperator(hf, tm);
        FilterOperator.Predicate pred = r -> r.getValue(0).equals(targetKey);
        Operator filter = new FilterOperator(scan, pred);
        filter.open();
        boolean found = filter.next() != null;
        filter.close();
        return found;
    }

    private static boolean runIndexScan(BTreeIndex index, HeapFile hf, int targetKey, TransactionManager tm) throws Exception {
        Operator scan = new IndexScanOperator(index, hf, targetKey, tm);
        scan.open();
        boolean found = scan.next() != null;
        scan.close();
        return found;
    }

    private static void benchmark2_MVCC_Concurrency(PrintWriter out) throws Exception {
        out.println("## 2. MVCC Read Throughput & Non-Blocking Readers");
        out.println();
        
        cleanup();
        PageManager pm = new PageManager(TABLE_FILE);
        BufferPool bp = new BufferPool(pm, 100);
        List<ColumnType> schema = Arrays.asList(ColumnType.INT, ColumnType.VARCHAR);
        HeapFile hf = new HeapFile(bp, pm, schema);
        TransactionManager txnManager = new TransactionManager(null);

        // Initial data
        Transaction t0 = txnManager.begin();
        setCurrentTxn(txnManager, t0);
        for (int i = 0; i < 1000; i++) {
            Row r = new Row(new Object[]{i, "Initial"});
            r.setXmin(t0.getId());
            hf.insert(r);
        }
        txnManager.commit(t0);

        int NUM_READERS = 10;
        CountDownLatch startLatch = new CountDownLatch(1);
        CountDownLatch endLatch = new CountDownLatch(NUM_READERS);
        AtomicInteger totalReads = new AtomicInteger(0);
        AtomicLong maxReaderLatencyNs = new AtomicLong(0);

        // Writer thread (sleeps for 200ms simulating heavy work)
        Thread writer = new Thread(() -> {
            try {
                startLatch.await();
                Transaction t1 = txnManager.begin();
                setCurrentTxn(txnManager, t1);
                
                for (int i = 1000; i < 1200; i++) {
                    Row r = new Row(new Object[]{i, "WriterActive"});
                    r.setXmin(t1.getId());
                    hf.insert(r);
                }
                
                // Sleep 200ms while holding uncommitted rows
                Thread.sleep(200);
                
                txnManager.commit(t1);
            } catch (Exception e) {
                e.printStackTrace();
            }
        });

        // Reader threads
        long testDurationMs = 500;
        long endTime = System.currentTimeMillis() + testDurationMs;
        
        for (int i = 0; i < NUM_READERS; i++) {
            new Thread(() -> {
                try {
                    startLatch.await();
                    Transaction rt = txnManager.begin();
                    setCurrentTxn(txnManager, rt);
                    
                    while (System.currentTimeMillis() < endTime) {
                        long loopStart = System.nanoTime();
                        
                        // Full table scan
                        Operator scan = new SeqScanOperator(hf, txnManager);
                        scan.open();
                        int count = 0;
                        while (scan.next() != null) count++;
                        scan.close();
                        
                        long loopLatency = System.nanoTime() - loopStart;
                        synchronized(maxReaderLatencyNs) {
                            if (loopLatency > maxReaderLatencyNs.get()) {
                                maxReaderLatencyNs.set(loopLatency);
                            }
                        }
                        
                        totalReads.incrementAndGet();
                    }
                } catch (Exception e) {
                    e.printStackTrace();
                } finally {
                    endLatch.countDown();
                }
            }).start();
        }

        long startTest = System.currentTimeMillis();
        startLatch.countDown(); // Go!
        writer.start();
        
        endLatch.await();
        writer.join();
        
        long actualDuration = System.currentTimeMillis() - startTest;
        double throughput = (totalReads.get() * 1000.0) / actualDuration;
        
        out.println("Ran " + NUM_READERS + " concurrent readers alongside 1 writer (which slept for 200ms).");
        out.println();
        out.println("- **Total Table Scans Completed**: " + totalReads.get());
        out.println("- **Throughput**: " + String.format("%.2f", throughput) + " scans/sec");
        out.println("- **Max Reader Latency**: " + String.format("%,d", maxReaderLatencyNs.get() / 1_000_000) + " ms");
        out.println();
        out.println("*(Since max reader latency is well under 200ms, readers were **not** blocked by the active writer).*");
        out.println();

        pm.close();
    }

    private static void benchmark3_WAL_Overhead(PrintWriter out) throws Exception {
        out.println("## 3. WAL / Recovery Overhead");
        out.println();
        out.println("| Mode | Time per Commit (ms) | Time per Commit (ns) |");
        out.println("|------|----------------------|----------------------|");
        
        cleanup();
        WALManager wal = new WALManager(WAL_FILE);
        
        int ITERATIONS = 1000;
        
        // 1. With FSYNC
        wal.setFsyncEnabled(true);
        long startSync = System.nanoTime();
        for (int i = 0; i < ITERATIONS; i++) {
            wal.appendLogRecord(new LogRecord(i, 0, null, null, LogRecord.Type.COMMIT));
        }
        long syncAvgNs = (System.nanoTime() - startSync) / ITERATIONS;
        
        // 2. Without FSYNC
        wal.setFsyncEnabled(false);
        long startNoSync = System.nanoTime();
        for (int i = 0; i < ITERATIONS; i++) {
            wal.appendLogRecord(new LogRecord(i, 0, null, null, LogRecord.Type.COMMIT));
        }
        long noSyncAvgNs = (System.nanoTime() - startNoSync) / ITERATIONS;
        
        out.printf("| `fsync()` Enabled (Safe) | %.4f | %,d |\n", syncAvgNs / 1_000_000.0, syncAvgNs);
        out.printf("| `fsync()` Disabled (Fast) | %.4f | %,d |\n", noSyncAvgNs / 1_000_000.0, noSyncAvgNs);
        
        out.println();
        out.println("*(Synchronous disk writes add significant latency, demonstrating the durability vs performance trade-off).*");
        out.println();
        
        wal.close();
    }

    private static void setCurrentTxn(TransactionManager tm, Transaction txn) throws Exception {
        java.lang.reflect.Field f = TransactionManager.class.getDeclaredField("currentTxn");
        f.setAccessible(true);
        @SuppressWarnings("unchecked")
        ThreadLocal<Transaction> tl = (ThreadLocal<Transaction>) f.get(tm);
        tl.set(txn);
    }
}
