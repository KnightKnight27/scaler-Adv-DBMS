package com.minidb.storage;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class Phase1FinalVerification {

    private record Entry(RowId rowId, int id, String name) {}

    public static void main(String[] args) {
        String dbPath = "phase1_final_verify.db";
        File dbFile = new File(dbPath);

        if (dbFile.exists()) {
            dbFile.delete();
        }

        List<ColumnType> schema = List.of(ColumnType.INT, ColumnType.VARCHAR);
        List<Entry> groundTruth = new ArrayList<>();

        try {
            // 1. Insert 1000 rows
            PageManager pm = new PageManager(dbPath);
            BufferPool bp = new BufferPool(pm, 100);
            HeapFile hf = new HeapFile(bp, pm, schema);

            for (int id = 0; id < 1000; id++) {
                String name = "Name_" + id + "x".repeat(id % 200);
                Row row = new Row(new Object[]{id, name});
                RowId rowId = hf.insert(row);
                groundTruth.add(new Entry(rowId, id, name));
            }

            // 2. Flush and close
            bp.flushAll();
            pm.close();

            // 3. Reopen
            PageManager pmRestart = new PageManager(dbPath);
            BufferPool bpRestart = new BufferPool(pmRestart, 100);
            HeapFile hfRestart = new HeapFile(bpRestart, pmRestart, schema);

            // 4. Verification via get(rowId)
            int exactMatches = 0;
            int mismatches = 0;
            int readErrors = 0;
            List<String> mismatchLog = new ArrayList<>();
            List<Throwable> errorLog = new ArrayList<>();

            for (Entry entry : groundTruth) {
                try {
                    Row retrieved = hfRestart.get(entry.rowId);
                    if (retrieved == null) {
                        mismatches++;
                        if (mismatchLog.size() < 3) {
                            mismatchLog.add(String.format("RowID %s: Expected (%d, %s), Got null", entry.rowId, entry.id, entry.name));
                        }
                    } else {
                        int rId = (int) retrieved.getValue(0);
                        String rName = (String) retrieved.getValue(1);

                        if (rId == entry.id && rName.equals(entry.name)) {
                            exactMatches++;
                        } else {
                            mismatches++;
                            if (mismatchLog.size() < 3) {
                                mismatchLog.add(String.format("RowID %s: Expected (%d, %s), Got (%d, %s)", 
                                    entry.rowId, entry.id, entry.name, rId, rName));
                            }
                        }
                    }
                } catch (Exception e) {
                    readErrors++;
                    if (errorLog.size() < 3) {
                        errorLog.add(e);
                    }
                }
            }

            // 5. Printing get() results
            System.out.println("TOTAL_ROWS_INSERTED: 1000");
            System.out.println("EXACT_MATCHES: " + exactMatches);
            System.out.println("MISMATCHES: " + mismatches);
            for (String m : mismatchLog) {
                System.out.println("  Mismatch: " + m);
            }
            System.out.println("READ_ERRORS: " + readErrors);
            for (Throwable t : errorLog) {
                t.printStackTrace(System.out);
            }

            // 6. Verification via scan()
            List<Row> scanResults = hfRestart.scan();
            Set<Integer> uniqueIds = new HashSet<>();
            for (Row r : scanResults) {
                uniqueIds.add((int) r.getValue(0));
            }

            List<Integer> missingIds = new ArrayList<>();
            for (int i = 0; i < 1000; i++) {
                if (!uniqueIds.contains(i)) {
                    missingIds.add(i);
                }
            }

            System.out.println("SCAN_ROW_COUNT: " + scanResults.size());
            System.out.println("SCAN_UNIQUE_IDS: " + uniqueIds.size());
            System.out.print("SCAN_MISSING_IDS: ");
            if (missingIds.isEmpty()) {
                System.out.println("None");
            } else {
                System.out.println(missingIds);
            }
            System.out.println("SCAN_DUPLICATE_COUNT: " + (scanResults.size() - uniqueIds.size()));

            pmRestart.close();

        } catch (Exception e) {
            System.err.println("CRITICAL ERROR DURING VERIFICATION:");
            e.printStackTrace();
        }
    }
}
