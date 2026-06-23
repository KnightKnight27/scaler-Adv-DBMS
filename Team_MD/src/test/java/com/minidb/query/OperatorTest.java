package com.minidb.query;

import com.minidb.index.BTreeIndex;
import com.minidb.query.operators.*;
import com.minidb.storage.*;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.io.File;
import java.util.Arrays;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

public class OperatorTest {

    private static final String TABLE_FILE = "test_table_op.db";
    private static final String INDEX_FILE = "test_index_op.db";

    private PageManager pageManager;
    private HeapFile heapFile;
    private List<ColumnType> schema;

    @BeforeEach
    public void setup() throws Exception {
        new File(TABLE_FILE).delete();
        new File(INDEX_FILE).delete();
        schema = Arrays.asList(ColumnType.INT, ColumnType.VARCHAR);
        pageManager = new PageManager(TABLE_FILE);
        heapFile = new HeapFile(/* bufferPool */ new BufferPool(pageManager, 32), pageManager, schema);
    }

    @AfterEach
    public void teardown() throws Exception {
        pageManager.close();
        new File(TABLE_FILE).delete();
        new File(INDEX_FILE).delete();
    }

    // -----------------------------------------------------------------------
    // SeqScan
    // -----------------------------------------------------------------------
    @Test
    public void testSeqScan() throws Exception {
        for (int i = 0; i < 10; i++) {
            heapFile.insert(new Row(new Object[]{i, "User" + i}));
        }

        SeqScanOperator op = new SeqScanOperator(heapFile);
        op.open();
        int count = 0;
        Row row;
        while ((row = op.next()) != null) count++;
        op.close();

        assertEquals(10, count);
    }

    // -----------------------------------------------------------------------
    // IndexScan
    // -----------------------------------------------------------------------
    @Test
    public void testIndexScan() throws Exception {
        BTreeIndex index = new BTreeIndex(INDEX_FILE);
        try {
            for (int i = 0; i < 20; i++) {
                RowId rowId = heapFile.insert(new Row(new Object[]{i, "User" + i}));
                index.insert(i, rowId);
            }

            IndexScanOperator op = new IndexScanOperator(index, heapFile, 10);
            op.open();
            Row result = op.next();
            op.close();

            assertNotNull(result);
            assertEquals(10, result.getValue(0));
            assertEquals("User10", result.getValue(1));
        } finally {
            index.close();
        }
    }

    // -----------------------------------------------------------------------
    // Filter
    // -----------------------------------------------------------------------
    @Test
    public void testFilter() throws Exception {
        for (int i = 0; i < 20; i++) {
            heapFile.insert(new Row(new Object[]{i, "User" + i}));
        }

        // Filter: id >= 10
        FilterOperator.Predicate pred = row -> (int) row.getValue(0) >= 10;
        FilterOperator op = new FilterOperator(new SeqScanOperator(heapFile), pred);

        op.open();
        int count = 0;
        Row row;
        while ((row = op.next()) != null) {
            assertTrue((int) row.getValue(0) >= 10);
            count++;
        }
        op.close();

        assertEquals(10, count);
    }

    // -----------------------------------------------------------------------
    // Project
    // -----------------------------------------------------------------------
    @Test
    public void testProject() throws Exception {
        heapFile.insert(new Row(new Object[]{42, "Alice"}));

        // Project only column index 1 (VARCHAR name)
        ProjectOperator op = new ProjectOperator(new SeqScanOperator(heapFile), Arrays.asList(1));
        op.open();
        Row result = op.next();
        op.close();

        assertNotNull(result);
        assertEquals(1, result.getValues().length);
        assertEquals("Alice", result.getValue(0));
    }

    // -----------------------------------------------------------------------
    // Join
    // -----------------------------------------------------------------------
    @Test
    public void testJoin() throws Exception {
        // left: id, name
        for (int i = 0; i < 3; i++) {
            heapFile.insert(new Row(new Object[]{i, "User" + i}));
        }

        // right table (separate file)
        String rightFile = "test_right_op.db";
        new File(rightFile).delete();
        PageManager rightPM = new PageManager(rightFile);
        List<ColumnType> rightSchema = Arrays.asList(ColumnType.INT, ColumnType.VARCHAR);
        HeapFile rightHeap = new HeapFile(new BufferPool(rightPM, 32), rightPM, rightSchema);
        for (int i = 0; i < 3; i++) {
            rightHeap.insert(new Row(new Object[]{i, "Dept" + i}));
        }

        JoinOperator join = new JoinOperator(
            new SeqScanOperator(heapFile),
            new SeqScanOperator(rightHeap),
            0, 0  // join on id == id
        );

        join.open();
        int count = 0;
        Row row;
        while ((row = join.next()) != null) {
            assertEquals(4, row.getValues().length); // 2 + 2 columns merged
            count++;
        }
        join.close();

        rightPM.close();
        new File(rightFile).delete();

        assertEquals(3, count);  // 3 matching pairs
    }

    // -----------------------------------------------------------------------
    // Insert
    // -----------------------------------------------------------------------
    @Test
    public void testInsert() throws Exception {
        List<Row> rows = Arrays.asList(
            new Row(new Object[]{1, "A"}),
            new Row(new Object[]{2, "B"}),
            new Row(new Object[]{3, "C"})
        );

        InsertOperator insert = new InsertOperator(heapFile, rows);
        insert.open();
        Row countRow = insert.next();
        insert.close();

        assertNotNull(countRow);
        assertEquals(3, countRow.getValue(0));

        // Verify data was inserted
        SeqScanOperator scan = new SeqScanOperator(heapFile);
        scan.open();
        int count = 0;
        while (scan.next() != null) count++;
        scan.close();

        assertEquals(3, count);
    }

    // -----------------------------------------------------------------------
    // Optimizer: IndexScan chosen when index exists
    // -----------------------------------------------------------------------
    @Test
    public void testOptimizerUsesIndexScan() throws Exception {
        BTreeIndex index = new BTreeIndex(INDEX_FILE);
        try {
            for (int i = 0; i < 50; i++) {
                RowId rowId = heapFile.insert(new Row(new Object[]{i, "User" + i}));
                index.insert(i, rowId);
            }

            QueryOptimizer optimizer = new QueryOptimizer();
            // searchKey=25 with an index → should use IndexScan
            Operator op = optimizer.optimize("SELECT * FROM users WHERE id = 25",
                heapFile, index, null, 25, null);

            assertTrue(op instanceof IndexScanOperator,
                "Optimizer should pick IndexScanOperator, got: " + op.getClass().getSimpleName());

            op.open();
            Row result = op.next();
            op.close();

            assertNotNull(result);
            assertEquals(25, result.getValue(0));
        } finally {
            index.close();
        }
    }

    // -----------------------------------------------------------------------
    // Optimizer: SeqScan + Filter chosen when no index
    // -----------------------------------------------------------------------
    @Test
    public void testOptimizerUsesSeqScan() throws Exception {
        for (int i = 0; i < 10; i++) {
            heapFile.insert(new Row(new Object[]{i, "User" + i}));
        }

        QueryOptimizer optimizer = new QueryOptimizer();
        FilterOperator.Predicate pred = row -> (int) row.getValue(0) == 5;
        // no index → SeqScan + Filter
        Operator op = optimizer.optimize("SELECT * FROM users WHERE id = 5",
            heapFile, null, pred, -1, null);

        assertTrue(op instanceof FilterOperator,
            "Optimizer should use FilterOperator over SeqScan, got: " + op.getClass().getSimpleName());

        op.open();
        Row result = op.next();
        op.close();

        assertNotNull(result);
        assertEquals(5, result.getValue(0));
    }

    // -----------------------------------------------------------------------
    // End-to-end: Filter + Project on SeqScan
    // -----------------------------------------------------------------------
    @Test
    public void testEndToEndFilterAndProject() throws Exception {
        for (int i = 0; i < 100; i++) {
            heapFile.insert(new Row(new Object[]{i, "User" + i}));
        }

        // SELECT name FROM users WHERE id = 77
        FilterOperator.Predicate pred = row -> (int) row.getValue(0) == 77;
        Operator op = new ProjectOperator(
            new FilterOperator(new SeqScanOperator(heapFile), pred),
            Arrays.asList(1)  // project column index 1 = name
        );

        op.open();
        Row result = op.next();
        assertNull(op.next());  // only one result
        op.close();

        assertNotNull(result);
        assertEquals("User77", result.getValue(0));
    }
}
