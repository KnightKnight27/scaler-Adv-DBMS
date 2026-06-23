package com.minidb.query;

import com.minidb.index.BTreeIndex;
import com.minidb.query.operators.FilterOperator;
import com.minidb.query.operators.IndexScanOperator;
import com.minidb.query.operators.JoinOperator;
import com.minidb.query.operators.ProjectOperator;
import com.minidb.query.operators.SeqScanOperator;
import com.minidb.storage.*;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.io.File;
import java.util.Arrays;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class OptimizerTest {

    private static final String TABLE_1_FILE = "opt_table1.db";
    private static final String TABLE_2_FILE = "opt_table2.db";
    private static final String INDEX_FILE   = "opt_index.db";

    private PageManager pm1;
    private PageManager pm2;
    private HeapFile heapFile1;
    private HeapFile heapFile2;
    private BTreeIndex index;
    private QueryOptimizer optimizer;

    @BeforeEach
    public void setup() throws Exception {
        new File(TABLE_1_FILE).delete();
        new File(TABLE_2_FILE).delete();
        new File(INDEX_FILE).delete();

        pm1 = new PageManager(TABLE_1_FILE);
        pm2 = new PageManager(TABLE_2_FILE);

        List<ColumnType> schema = Arrays.asList(ColumnType.INT, ColumnType.VARCHAR);
        BufferPool bp1 = new BufferPool(pm1, 10);
        BufferPool bp2 = new BufferPool(pm2, 10);

        heapFile1 = new HeapFile(bp1, pm1, schema);
        heapFile2 = new HeapFile(bp2, pm2, schema);

        index = new BTreeIndex(INDEX_FILE);
        optimizer = new QueryOptimizer();
    }

    @AfterEach
    public void teardown() throws Exception {
        pm1.close();
        pm2.close();
        index.close();

        new File(TABLE_1_FILE).delete();
        new File(TABLE_2_FILE).delete();
        new File(INDEX_FILE).delete();
    }

    @Test
    public void testSelectivityAndScanChoice() {
        FilterOperator.Predicate equalityPred = r -> r.getValue(0).equals(42);
        
        // Case 1: Indexed equality filter -> high selectivity -> IndexScan
        Operator op1 = optimizer.optimize("SELECT *", heapFile1, index, equalityPred, 42, null);
        assertTrue(op1 instanceof IndexScanOperator, "Expected IndexScanOperator for high selectivity");

        // Case 2: No index provided -> low selectivity -> SeqScan
        Operator op2 = optimizer.optimize("SELECT *", heapFile1, null, equalityPred, 42, null);
        assertTrue(op2 instanceof FilterOperator, "Expected FilterOperator wrapping SeqScan");
        
        // Use reflection to unwrap FilterOperator and check its child
        try {
            java.lang.reflect.Field childField = FilterOperator.class.getDeclaredField("child");
            childField.setAccessible(true);
            Operator childOp = (Operator) childField.get(op2);
            assertTrue(childOp instanceof SeqScanOperator, "Expected SeqScanOperator under FilterOperator");
        } catch (Exception e) {
            throw new RuntimeException(e);
        }

        // Case 3: No equality filter key (-1) -> low selectivity -> SeqScan
        Operator op3 = optimizer.optimize("SELECT *", heapFile1, index, equalityPred, -1, null);
        assertTrue(op3 instanceof FilterOperator, "Expected FilterOperator when no search key is provided");
    }

    @Test
    public void testJoinOrderSelection() throws Exception {
        // Populate table 1 with 5 rows
        for (int i = 0; i < 5; i++) {
            heapFile1.insert(new Row(new Object[]{i, "A"}));
        }
        
        // Populate table 2 with 20 rows
        for (int i = 0; i < 20; i++) {
            heapFile2.insert(new Row(new Object[]{i, "B"}));
        }

        // Case 1: Join T1 and T2. T1 is smaller (5 vs 20), so T1 should drive the outer loop.
        Operator join1 = optimizer.optimizeJoin(heapFile1, heapFile2, 0, 0, null);
        assertTrue(join1 instanceof JoinOperator);
        
        try {
            java.lang.reflect.Field leftField = JoinOperator.class.getDeclaredField("left");
            leftField.setAccessible(true);
            Operator leftOp = (Operator) leftField.get(join1);
            
            java.lang.reflect.Field hfField = SeqScanOperator.class.getDeclaredField("heapFile");
            hfField.setAccessible(true);
            HeapFile leftHf = (HeapFile) hfField.get(leftOp);
            
            assertEquals(heapFile1, leftHf, "Table 1 is smaller and should be the outer (left) loop");
        } catch (Exception e) {
            throw new RuntimeException(e);
        }

        // Add 50 rows to T1, making it larger (55 vs 20)
        for (int i = 0; i < 50; i++) {
            heapFile1.insert(new Row(new Object[]{i, "C"}));
        }

        // Case 2: Join T1 and T2. T2 is now smaller (20 vs 55), so T2 should drive the outer loop.
        Operator join2 = optimizer.optimizeJoin(heapFile1, heapFile2, 0, 0, null);
        
        try {
            java.lang.reflect.Field leftField = JoinOperator.class.getDeclaredField("left");
            leftField.setAccessible(true);
            Operator leftOp = (Operator) leftField.get(join2);
            
            java.lang.reflect.Field hfField = SeqScanOperator.class.getDeclaredField("heapFile");
            hfField.setAccessible(true);
            HeapFile leftHf = (HeapFile) hfField.get(leftOp);
            
            assertEquals(heapFile2, leftHf, "Table 2 is now smaller and should be the outer (left) loop");
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }
}
