package com.minidb.storage;

import java.io.IOException;
import java.io.File;
import java.util.List;

public class CheckFileSizeTest {
    public static void main(String[] args) throws IOException {
        String path = "minidb_verify.db";
        PageManager pm = new PageManager(path);
        BufferPool bp = new BufferPool(pm, 10);
        HeapFile hf = new HeapFile(bp, pm, List.of(ColumnType.INT, ColumnType.VARCHAR));

        System.out.println("Inserting data to fill multiple pages...");
        for (int i = 0; i < 200; i++) {
            hf.insert(new Row(new Object[]{i, "Data_" + i + "x".repeat(50)}));
        }
        
        bp.flushAll();
        int numPages = pm.getNumPages();
        pm.close();

        File file = new File(path);
        long size = file.length();
        System.out.println("Number of pages: " + numPages);
        System.out.println("Expected size: " + (numPages * 4096) + " bytes");
        System.out.println("Actual file size: " + size + " bytes");
        
        if (size == (long) numPages * 4096) {
            System.out.println("VERIFICATION: SUCCESS - File size matches exactly numPages * 4096.");
        } else {
            System.out.println("VERIFICATION: FAILED - File size mismatch!");
        }
    }
}
