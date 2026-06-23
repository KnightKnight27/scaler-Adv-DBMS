package com.minidb.storage;

import java.util.List;

public class DBViewer {
    public static void main(String[] args) {
        String dbPath = "minidb.db"; 
        
        System.out.println("Opening database: " + dbPath);
        
        try {
            PageManager pm = new PageManager(dbPath);
            BufferPool bp = new BufferPool(pm, 10, null);
            
            // Assuming the schema is INT, VARCHAR as in the integration tests
            List<ColumnType> schema = List.of(ColumnType.INT, ColumnType.VARCHAR);
            HeapFile hf = new HeapFile(bp, pm, schema, null);
            
            List<Row> rows = hf.scan();
            System.out.println("--- Database Contents ---");
            System.out.println("Total Rows: " + rows.size());
            
            for (int i = 0; i < Math.min(rows.size(), 100); i++) {
                System.out.println("Row " + i + ": " + rows.get(i).toString());
            }
            
            if (rows.size() > 100) {
                System.out.println("... (showing only first 100 rows)");
            }
            
            pm.close();
            System.out.println("--- End of Database ---");
            
        } catch (Exception e) {
            System.err.println("Error reading database: " + e.getMessage());
            e.printStackTrace();
        }
    }
}
