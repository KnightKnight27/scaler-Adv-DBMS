package com.minidb.storage;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.*;

/**
 * Detailed verification report for Phase 1 Storage Engine.
 */
public class Phase1VerificationReport {

    private record Entry(RowId rowId, int id, String name) {}

    public static void main(String[] args) {
        String dbPath = "phase1_verify.db";
        File dbFile = new File(dbPath);

        // 1. Start clean
        if (dbFile.exists()) {
            dbFile.delete();
        }

        List<ColumnType> schema = List.of(ColumnType.INT, ColumnType.VARCHAR);
        List<Entry> groundTruth = new ArrayList<>();

        System.out.println("=== Phase 1 Independent Verification Report ===");
        System.out.println("Step 1: Initializing fresh HeapFile at " + dbPath);

        try {
            // 2 & 3. Create and Insert
            PageManager pm = new PageManager(dbPath);
            BufferPool bp = new BufferPool(pm, 50);
            HeapFile hf = new HeapFile(bp, pm, schema);

            System.out.println("Step 2: Inserting 1000 rows with varying lengths...");
            int minNameBytes = Integer.MAX_VALUE;
            int maxNameBytes = 0;

            for (int id = 0; id < 1000; id++) {
                // Varying name length: "Name_" + id + optional padding
                String name = "Name_" + id + "x".repeat(id % 200);
                byte[] nameBytes = name.getBytes(StandardCharsets.UTF_8);
                minNameBytes = Math.min(minNameBytes, nameBytes.length);
                maxNameBytes = Math.max(maxNameBytes, nameBytes.length);

                Row row = new Row(new Object[]{id, name});
                RowId rowId = hf.insert(row);
                groundTruth.add(new Entry(rowId, id, name));
            }

            // 4. Print stats before shutdown
            int pagesAllocated = pm.getNumPages();
            long fileSizeBytes = dbFile.length();
            long remainder = fileSizeBytes % 4096;

            System.out.println("\n--- Post-Insert Statistics ---");
            System.out.println("Total Pages Allocated:   " + pagesAllocated);
            System.out.println("Total File Size:         " + fileSizeBytes + " bytes");
            System.out.println("Size / 4096 Remainder:   " + remainder);
            System.out.println("Min Name Length (bytes): " + minNameBytes);
            System.out.println("Max Name Length (bytes): " + maxNameBytes);

            // 5. Shutdown
            System.out.println("\nStep 3: Flushing buffer pool and closing manager...");
            bp.flushAll();
            pm.close();
            System.out.println("Database file remains on disk for inspection.");

            // 6. Simulate Restart
            System.out.println("\nStep 4: Restarting system (Fresh instances)...");
            PageManager pmRestart = new PageManager(dbPath);
            BufferPool bpRestart = new BufferPool(pmRestart, 50);
            HeapFile hfRestart = new HeapFile(bpRestart, pmRestart, schema);

            // 7. Verification via get(rowId)
            System.out.println("Step 5: Verifying all 1000 original RowIds via get(rowId)...");
            int matches = 0;
            int mismatches = 0;
            int exceptions = 0;
            List<String> mismatchDetails = new ArrayList<>();

            for (Entry entry : groundTruth) {
                try {
                    Row retrieved = hfRestart.get(entry.rowId);
                    if (retrieved == null) {
                        mismatches++;
                        if (mismatchDetails.size() < 5) mismatchDetails.add("RowID " + entry.rowId + " returned null");
                    } else {
                        int rId = (int) retrieved.getValue(0);
                        String rName = (String) retrieved.getValue(1);

                        if (rId == entry.id && rName.equals(entry.name)) {
                            matches++;
                        } else {
                            mismatches++;
                            if (mismatchDetails.size() < 5) {
                                mismatchDetails.add(String.format("Mismatch at RowID %s: Expected (%d, %s), Got (%d, %s)", 
                                    entry.rowId, entry.id, entry.name, rId, rName));
                            }
                        }
                    }
                } catch (Exception e) {
                    exceptions++;
                    if (mismatchDetails.size() < 5) mismatchDetails.add("Exception reading RowID " + entry.rowId + ": " + e.getMessage());
                }
            }

            System.out.println("Exact Matches: " + matches);
            System.out.println("Mismatches:    " + mismatches);
            System.out.println("Exceptions:    " + exceptions);
            for (String detail : mismatchDetails) {
                System.out.println("  [Detail] " + detail);
            }

            // 8. Verification via scan()
            System.out.println("\nStep 6: Verifying data integrity via scan()...");
            List<Row> scanResults = hfRestart.scan();
            Set<Integer> uniqueIds = new HashSet<>();
            int duplicates = 0;
            for (Row r : scanResults) {
                int id = (int) r.getValue(0);
                if (!uniqueIds.add(id)) {
                    duplicates++;
                }
            }

            int missingCount = 0;
            for (int i = 0; i < 1000; i++) {
                if (!uniqueIds.contains(i)) missingCount++;
            }

            System.out.println("Scan Total Rows: " + scanResults.size());
            System.out.println("Unique IDs:      " + uniqueIds.size());
            System.out.println("Duplicates:      " + duplicates);
            System.out.println("Gaps/Missing:    " + missingCount);

            // 9. Summary Table
            System.out.println("\n=== FINAL VERIFICATION SUMMARY ===");
            System.out.printf("[%-40s] %s\n", "File size is exact multiple of 4096", (remainder == 0 ? "PASS" : "FAIL"));
            System.out.printf("[%-40s] %s\n", "1000 rows match via get(rowId)", (matches == 1000 ? "PASS" : "FAIL"));
            System.out.printf("[%-40s] %s\n", "scan() returns 1000 unique rows", (uniqueIds.size() == 1000 && duplicates == 0 ? "PASS" : "FAIL"));
            System.out.printf("[%-40s] %s\n", "Multi-page logic exercised (>1 page)", (pagesAllocated > 1 ? "PASS" : "FAIL"));
            System.out.println("===================================");

            pmRestart.close();

        } catch (IOException e) {
            System.err.println("CRITICAL: IO Error during verification:");
            e.printStackTrace();
        }
    }
}
