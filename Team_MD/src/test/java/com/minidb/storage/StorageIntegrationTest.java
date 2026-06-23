package com.minidb.storage;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/**
 * Integration test for Phase 1: Storage Engine.
 */
public class StorageIntegrationTest {

    public static void main(String[] args) {
        String dbPath = "minidb.db";
        // Clean up previous test run
        // new File(dbPath).delete();

        List<ColumnType> schema = List.of(ColumnType.INT, ColumnType.VARCHAR);
        List<Row> originalRows = new ArrayList<>();

        System.out.println("--- Starting Phase 1 Integration Test ---");

        try {
            // 1. Setup Phase 1 components
            PageManager pm = new PageManager(dbPath);
            BufferPool bp = new BufferPool(pm, 50);
            HeapFile hf = new HeapFile(bp, pm, schema);

            // 2. Insert 1000 rows
            System.out.println("Inserting 1000 rows...");
            for (int i = 0; i < 1000; i++) {
                String name = "User_" + i + "_" + "A".repeat(i % 50); // Varying lengths
                Row row = new Row(new Object[] { i, name });
                hf.insert(row);
                originalRows.add(row);
            }

            // 3. Simulated clean shutdown
            System.out.println("Flushing and closing...");
            bp.flushAll();
            pm.close();

            // 4. Simulate Restart: Create new instances pointing to the same file
            System.out.println("Restarting system...");
            PageManager pmRestart = new PageManager(dbPath);
            BufferPool bpRestart = new BufferPool(pmRestart, 50);
            HeapFile hfRestart = new HeapFile(bpRestart, pmRestart, schema);

            // 5. Scan and Verify
            System.out.println("Scanning and verifying (sorting by ID)...");
            List<Row> retrievedRows = hfRestart.scan();

            // Sort both lists by ID (the first column) to compare multiset equality
            originalRows.sort((a, b) -> (Integer) a.getValue(0) - (Integer) b.getValue(0));
            retrievedRows.sort((a, b) -> (Integer) a.getValue(0) - (Integer) b.getValue(0));

            boolean pass = true;
            if (retrievedRows.size() != originalRows.size()) {
                System.err.println("FAIL: Row count mismatch. Expected: " + originalRows.size() + ", Got: "
                        + retrievedRows.size());
                pass = false;
            } else {
                for (int i = 0; i < originalRows.size(); i++) {
                    if (!originalRows.get(i).equals(retrievedRows.get(i))) {
                        System.err.println("FAIL: Content mismatch at index " + i);
                        System.err.println("  Expected: " + originalRows.get(i));
                        System.err.println("  Got:      " + retrievedRows.get(i));
                        pass = false;
                        break;
                    }
                }
            }

            if (pass) {
                System.out.println("RESULT: PASS - All 1000 rows persisted and retrieved correctly.");
            } else {
                System.out.println("RESULT: FAIL - Data corruption or loss detected.");
            }

            pmRestart.close();
            // new File(dbPath).delete();

        } catch (IOException e) {
            System.err.println("An error occurred during testing:");
            e.printStackTrace();
        }
    }
}
