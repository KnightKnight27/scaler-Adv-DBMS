package com.minidb;

import com.minidb.index.BTreeIndex;
import com.minidb.query.Operator;
import com.minidb.query.QueryOptimizer;
import com.minidb.query.operators.*;

import com.minidb.recovery.WALManager;
import com.minidb.storage.*;
import com.minidb.txn.Transaction;
import com.minidb.txn.TransactionManager;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.io.File;
import java.util.Arrays;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

public class FinalIntegrationTest {

    private static final String TABLE_FILE = "integration_table.db";
    private static final String INDEX_FILE  = "integration_index.db";
    private static final String WAL_FILE    = "integration_wal.log";

    private PageManager pageManager;
    private HeapFile heapFile;
    private List<ColumnType> schema;
    private WALManager walManager;
    private TransactionManager txnManager;

    @BeforeEach
    public void setup() throws Exception {
        new File(TABLE_FILE).delete();
        new File(INDEX_FILE).delete();
        new File(WAL_FILE).delete();
        schema      = Arrays.asList(ColumnType.INT, ColumnType.VARCHAR);
        walManager  = new WALManager(WAL_FILE);
        txnManager  = new TransactionManager(walManager);
        pageManager = new PageManager(TABLE_FILE);
        heapFile    = new HeapFile(new BufferPool(pageManager, 32, walManager), pageManager, schema, walManager);
    }

    @AfterEach
    public void teardown() throws Exception {
        if (pageManager != null) pageManager.close();
        if (walManager  != null) walManager.close();
        new File(TABLE_FILE).delete();
        new File(INDEX_FILE).delete();
        new File(WAL_FILE).delete();
    }

    @Test
    public void test1_BasicStorageAndQuery() throws Exception {
        System.out.println("Running Test 1: Basic Storage & Query...");
        heapFile.insert(new Row(new Object[]{1, "Alice"}));
        heapFile.insert(new Row(new Object[]{2, "Bob"}));

        SeqScanOperator scan = new SeqScanOperator(heapFile);
        FilterOperator.Predicate pred = row -> (int) row.getValue(0) == 2;
        FilterOperator filter = new FilterOperator(scan, pred);

        filter.open();
        Row result = filter.next();
        assertNotNull(result, "Row should be found");
        assertEquals("Bob", result.getValue(1), "Expected Bob");
        assertNull(filter.next(), "Only one row should be returned");
        filter.close();
        System.out.println("Test 1 Passed!");
    }

    @Test
    public void test2_BPlusTreeAndOptimizer() throws Exception {
        System.out.println("Running Test 2: B+ Tree & Optimizer...");
        BTreeIndex index = new BTreeIndex(INDEX_FILE);

        try {
            for (int i = 0; i < 100; i++) {
                RowId rowId = heapFile.insert(new Row(new Object[]{i, "User" + i}));
                index.insert(i, rowId);
            }

            QueryOptimizer optimizer = new QueryOptimizer();
            FilterOperator.Predicate pred = row -> (int) row.getValue(0) == 50;
            Operator op1 = optimizer.optimize("SELECT * FROM users WHERE id = 50",
                heapFile, null, pred, -1, null, txnManager);
            assertTrue(op1 instanceof FilterOperator, "Should use SeqScan + Filter when no index provided");

            Operator op2 = optimizer.optimize("SELECT * FROM users WHERE id = 50",
                heapFile, index, null, 50, null, txnManager);
            assertTrue(op2 instanceof IndexScanOperator, "Should use IndexScan when index is available");

            op2.open();
            Row result = op2.next();
            assertNotNull(result);
            assertEquals("User50", result.getValue(1));
            op2.close();
            System.out.println("Test 2 Passed!");
        } finally {
            index.close();
        }
    }

    @Test
    public void test3_MVCCConcurrency() throws Exception {
        System.out.println("Running Test 3: MVCC Concurrency...");

        Transaction t1 = txnManager.begin();
        Transaction t2 = txnManager.begin();  // starts BEFORE t1 commits

        java.lang.reflect.Field tlField = TransactionManager.class.getDeclaredField("currentTxn");
        tlField.setAccessible(true);
        @SuppressWarnings("unchecked")
        ThreadLocal<Transaction> tl = (ThreadLocal<Transaction>) tlField.get(txnManager);

        tl.set(t1);
        InsertOperator insertOp = new InsertOperator(heapFile,
            Arrays.asList(new Row(new Object[]{100, "Concurrent"})), txnManager);
        insertOp.open();
        insertOp.next();
        insertOp.close();
        txnManager.commit(t1);

        // T2 should NOT see t1's row (snapshot taken before t1 committed)
        tl.set(t2);
        SeqScanOperator scan2 = new SeqScanOperator(heapFile, txnManager);
        scan2.open();
        Row r = scan2.next();
        scan2.close();
        assertNull(r, "T2 should NOT see T1's commit because T2 started before T1 committed");
        txnManager.commit(t2);

        // T3 starts after t1 committed → must see the row
        Transaction t3 = txnManager.begin();
        SeqScanOperator scan3 = new SeqScanOperator(heapFile, txnManager);
        scan3.open();
        Row r3 = scan3.next();
        scan3.close();
        assertNotNull(r3, "T3 should see T1's committed row");
        txnManager.commit(t3);
        System.out.println("Test 3 Passed!");
    }

    @Test
    public void test4_CrashRecovery() throws Exception {
        System.out.println("Running Test 4: Crash Recovery (Real Crash Simulation)...");

        // Close setup()-created handles so the subprocess can open the same paths
        pageManager.close();
        walManager.close();
        new File(TABLE_FILE).delete();
        new File(WAL_FILE).delete();

        String dbPath  = new File(TABLE_FILE).getAbsolutePath();
        String walPath = new File(WAL_FILE).getAbsolutePath();

        // ── PHASE 1: fork CrashSimulator in a child JVM ──────────────────
        String javaExec = ProcessHandle.current().info().command()
                .orElse(java.nio.file.Paths.get(System.getProperty("java.home"), "bin", "java").toString());

        ProcessBuilder pb = new ProcessBuilder(
                javaExec, "-cp", System.getProperty("java.class.path"),
                "com.minidb.recovery.CrashSimulator", dbPath, walPath);
        pb.redirectErrorStream(true);
        pb.directory(new File(System.getProperty("user.dir")));

        Process proc = pb.start();
        byte[] out   = proc.getInputStream().readAllBytes();
        int    code  = proc.waitFor();

        if (code != 42) System.err.println("CrashSimulator output:\n" + new String(out));
        assertEquals(42, code, "CrashSimulator must exit via Runtime.halt(42). Got: " + code);

        // ── PHASE 2: restart against same files; recovery runs automatically
        List<ColumnType> recSchema = Arrays.asList(ColumnType.INT, ColumnType.VARCHAR);
        WALManager  recWal = new WALManager(walPath);
        PageManager recPm  = new PageManager(dbPath);
        BufferPool  recBp  = new BufferPool(recPm, 32, recWal);   // triggers RecoveryManager
        HeapFile    recHf  = new HeapFile(recBp, recPm, recSchema, recWal);

        List<Row> rows = recHf.scan();

        boolean hasAlice   = rows.stream().anyMatch(r -> "Alice".equals(r.getValue(1)));
        boolean hasBob     = rows.stream().anyMatch(r -> "Bob".equals(r.getValue(1)));
        boolean hasCharlie = rows.stream().anyMatch(r -> "Charlie".equals(r.getValue(1)));

        assertTrue(hasAlice,    "Alice (committed) must be present after recovery");
        assertTrue(hasBob,      "Bob   (committed) must be present after recovery");
        assertFalse(hasCharlie, "Charlie (uncommitted) must NOT be present after recovery");
        assertEquals(2, rows.size(), "Exactly 2 rows expected after recovery, got: " + rows.size());

        recPm.close();
        recWal.close();
        pageManager = null;  // prevent @AfterEach double-close
        walManager  = null;

        System.out.println("Test 4 Passed! Recovery correctly restored " + rows.size()
                + " committed rows and removed all uncommitted data.");
    }
}
