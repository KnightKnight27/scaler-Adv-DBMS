package com.minidb.txn;

import com.minidb.query.operators.InsertOperator;
import com.minidb.query.operators.SeqScanOperator;
import com.minidb.storage.*;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.io.File;
import java.util.Arrays;
import java.util.List;
import java.util.ArrayList;

import static org.junit.jupiter.api.Assertions.*;

public class MVCCTest {
    private static final String TABLE_FILE = "mvcctest.db";
    private PageManager pageManager;
    private HeapFile heapFile;
    private List<ColumnType> schema;

    @BeforeEach
    public void setup() throws Exception {
        new File(TABLE_FILE).delete();
        schema = Arrays.asList(ColumnType.INT, ColumnType.VARCHAR);
        pageManager = new PageManager(TABLE_FILE);
        heapFile = new HeapFile(new BufferPool(pageManager, 32), pageManager, schema);
    }

    @AfterEach
    public void teardown() throws Exception {
        pageManager.close();
        new File(TABLE_FILE).delete();
    }

    @Test
    public void testMVCCVisibility() throws Exception {
        TransactionManager tm = new com.minidb.txn.TransactionManager(null);

        // txn2 starts before txn1 inserts
        Transaction txn2 = tm.begin();
        System.out.println("Txn " + txn2.getId() + " started (snapshot=" + txn2.getSnapshotId() + ")");

        // txn1 starts and inserts
        Transaction txn1 = tm.begin();
        System.out.println("Txn " + txn1.getId() + " started, inserting row...");
        List<Row> rowsToInsert = new ArrayList<>();
        rowsToInsert.add(new Row(new Object[]{1, "MVCC Row"}));
        InsertOperator insertOp = new InsertOperator(heapFile, rowsToInsert, tm);
        insertOp.open();
        insertOp.next();
        insertOp.close();
        
        System.out.println("Txn " + txn1.getId() + " committing...");
        tm.commit(txn1);

        // txn2 attempts to read (snapshot before txn1 committed)
        tm.getCurrent(); // ensure threadlocal is clear
        
        // Setup threadlocal context manually since we are jumping between txns on main thread
        java.lang.reflect.Field tlField = TransactionManager.class.getDeclaredField("currentTxn");
        tlField.setAccessible(true);
        @SuppressWarnings("unchecked")
        ThreadLocal<Transaction> tl = (ThreadLocal<Transaction>) tlField.get(tm);
        tl.set(txn2);

        System.out.println("Txn " + txn2.getId() + " scanning...");
        SeqScanOperator scan2 = new SeqScanOperator(heapFile, tm);
        scan2.open();
        Row r2 = scan2.next();
        scan2.close();
        assertNull(r2, "Txn 2 should NOT see Txn 1's inserted row (snapshot isolation)");
        System.out.println("Txn " + txn2.getId() + " saw 0 rows (correct!)");
        tm.commit(txn2);
        
        // txn3 starts after txn1 committed
        Transaction txn3 = tm.begin(); // internally sets threadlocal to txn3
        System.out.println("Txn " + txn3.getId() + " started (snapshot=" + txn3.getSnapshotId() + ")");
        System.out.println("Txn " + txn3.getId() + " scanning...");
        
        SeqScanOperator scan3 = new SeqScanOperator(heapFile, tm);
        scan3.open();
        Row r3 = scan3.next();
        scan3.close();
        assertNotNull(r3, "Txn 3 SHOULD see Txn 1's inserted row");
        System.out.println("Txn " + txn3.getId() + " saw row: " + r3.toString() + " (correct!)");
        tm.commit(txn3);
    }
}
