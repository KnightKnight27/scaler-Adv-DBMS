package com.minidb.recovery;

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

public class WALTest {

    private static final String TABLE_FILE = "wal_test_table.db";
    private static final String WAL_FILE   = "wal_test.log";

    private WALManager walManager;
    private TransactionManager txnManager;
    private PageManager pageManager;
    private HeapFile heapFile;

    @BeforeEach
    public void setup() throws Exception {
        new File(TABLE_FILE).delete();
        new File(WAL_FILE).delete();
        walManager  = new WALManager(WAL_FILE);
        txnManager  = new TransactionManager(walManager);
        pageManager = new PageManager(TABLE_FILE);
        List<ColumnType> schema = Arrays.asList(ColumnType.INT, ColumnType.VARCHAR);
        heapFile = new HeapFile(new BufferPool(pageManager, 32, walManager), pageManager, schema, walManager);
    }

    @AfterEach
    public void teardown() throws Exception {
        pageManager.close();
        walManager.close();
        new File(TABLE_FILE).delete();
        new File(WAL_FILE).delete();
    }

    /**
     * The WAL file must contain an INSERT record BEFORE the page is modified.
     * Verified by checking that log records exist and carry the correct type.
     */
    @Test
    public void testInsertLogsBeforePageWrite() throws Exception {
        Transaction t1 = txnManager.begin();

        // Wire the thread-local so that operators see the txn
        java.lang.reflect.Field tlField = TransactionManager.class.getDeclaredField("currentTxn");
        tlField.setAccessible(true);
        @SuppressWarnings("unchecked")
        ThreadLocal<Transaction> tl = (ThreadLocal<Transaction>) tlField.get(txnManager);
        tl.set(t1);

        heapFile.insert(new Row(new Object[]{1, "Alpha"}));
        heapFile.insert(new Row(new Object[]{2, "Beta"}));

        // WAL must have logged both inserts BEFORE this point
        List<LogRecord> records = walManager.readAllRecords();
        long insertCount = records.stream().filter(r -> r.getType() == LogRecord.Type.INSERT).count();
        assertEquals(2, insertCount, "Expected 2 INSERT log records before commit");

        // Every INSERT record must have a non-null afterImage
        for (LogRecord r : records) {
            if (r.getType() == LogRecord.Type.INSERT) {
                assertNotNull(r.getAfterImage(), "INSERT log record must have an afterImage");
                assertNull(r.getBeforeImage(), "INSERT log record must have null beforeImage");
            }
        }

        txnManager.commit(t1);
    }

    /**
     * A COMMIT log record must be written and flushed BEFORE commit() returns.
     */
    @Test
    public void testCommitRecordFlushedBeforeReturn() throws Exception {
        Transaction t1 = txnManager.begin();
        java.lang.reflect.Field tlField = TransactionManager.class.getDeclaredField("currentTxn");
        tlField.setAccessible(true);
        @SuppressWarnings("unchecked")
        ThreadLocal<Transaction> tl = (ThreadLocal<Transaction>) tlField.get(txnManager);
        tl.set(t1);

        heapFile.insert(new Row(new Object[]{42, "CommitTest"}));

        // Before commit, no COMMIT record exists
        List<LogRecord> beforeCommit = walManager.readAllRecords();
        long commitsBefore = beforeCommit.stream().filter(r -> r.getType() == LogRecord.Type.COMMIT).count();
        assertEquals(0, commitsBefore, "No COMMIT record should exist before commit() is called");

        txnManager.commit(t1);

        // After commit, exactly one COMMIT record must exist
        List<LogRecord> afterCommit = walManager.readAllRecords();
        long commitsAfter = afterCommit.stream().filter(r -> r.getType() == LogRecord.Type.COMMIT).count();
        assertEquals(1, commitsAfter, "Exactly 1 COMMIT record must be written when commit() is called");

        // The COMMIT record must carry t1's txnId
        boolean hasCorrectTxnId = afterCommit.stream()
            .anyMatch(r -> r.getType() == LogRecord.Type.COMMIT && r.getTxnId() == t1.getId());
        assertTrue(hasCorrectTxnId, "COMMIT log record must carry the correct txnId");
    }

    /**
     * An ABORT log record must be written when abort() is called.
     */
    @Test
    public void testAbortRecordWritten() throws Exception {
        Transaction t1 = txnManager.begin();
        java.lang.reflect.Field tlField = TransactionManager.class.getDeclaredField("currentTxn");
        tlField.setAccessible(true);
        @SuppressWarnings("unchecked")
        ThreadLocal<Transaction> tl = (ThreadLocal<Transaction>) tlField.get(txnManager);
        tl.set(t1);

        heapFile.insert(new Row(new Object[]{99, "WillAbort"}));
        txnManager.abort(t1);

        List<LogRecord> records = walManager.readAllRecords();
        long abortCount = records.stream().filter(r -> r.getType() == LogRecord.Type.ABORT).count();
        assertEquals(1, abortCount, "Exactly 1 ABORT record must be written on abort()");

        boolean hasCorrectTxnId = records.stream()
            .anyMatch(r -> r.getType() == LogRecord.Type.ABORT && r.getTxnId() == t1.getId());
        assertTrue(hasCorrectTxnId, "ABORT log record must carry the correct txnId");
    }

    /**
     * LSNs must be monotonically increasing.
     */
    @Test
    public void testLSNMonotonicallyIncreasing() throws Exception {
        Transaction t1 = txnManager.begin();
        java.lang.reflect.Field tlField = TransactionManager.class.getDeclaredField("currentTxn");
        tlField.setAccessible(true);
        @SuppressWarnings("unchecked")
        ThreadLocal<Transaction> tl = (ThreadLocal<Transaction>) tlField.get(txnManager);
        tl.set(t1);

        for (int i = 0; i < 5; i++) {
            heapFile.insert(new Row(new Object[]{i, "Row" + i}));
        }
        txnManager.commit(t1);

        List<LogRecord> records = walManager.readAllRecords();
        assertTrue(records.size() >= 6, "Expected at least 6 log records (5 INSERTs + 1 COMMIT)");

        long prevLSN = Long.MIN_VALUE;
        for (LogRecord r : records) {
            assertTrue(r.getLsn() > prevLSN,
                "LSN must be strictly increasing. Got " + r.getLsn() + " after " + prevLSN);
            prevLSN = r.getLsn();
        }
    }

    /**
     * UPDATE log record must carry both beforeImage and afterImage.
     */
    @Test
    public void testUpdateLogRecord() throws Exception {
        // Insert a row first (plain insert, no txn — xmin=0 so always visible)
        RowId rowId = heapFile.insert(new Row(new Object[]{10, "Before"}));

        // Now update it under a transaction
        Transaction t1 = txnManager.begin();
        java.lang.reflect.Field tlField = TransactionManager.class.getDeclaredField("currentTxn");
        tlField.setAccessible(true);
        @SuppressWarnings("unchecked")
        ThreadLocal<Transaction> tl = (ThreadLocal<Transaction>) tlField.get(txnManager);
        tl.set(t1);

        Row updated = new Row(new Object[]{10, "After!"});  // same byte-length as "Before"
        updated.setXmax(t1.getId());
        heapFile.update(rowId, updated);

        List<LogRecord> records = walManager.readAllRecords();
        long updateCount = records.stream().filter(r -> r.getType() == LogRecord.Type.UPDATE).count();
        assertEquals(1, updateCount, "Expected exactly 1 UPDATE log record");

        for (LogRecord r : records) {
            if (r.getType() == LogRecord.Type.UPDATE) {
                assertNotNull(r.getBeforeImage(), "UPDATE log record must have a beforeImage");
                assertNotNull(r.getAfterImage(), "UPDATE log record must have an afterImage");
            }
        }

        txnManager.commit(t1);
    }
}
