package com.minidb.storage;

import java.io.File;
import java.io.IOException;
import java.util.List;

/**
 * DATABASE DUMP TOOL - Forensic Storage Inspector
 * 
 * This utility provides a low-level view of the MiniDB binary heap files.
 * It reads directly from the PageManager to verify physical disk layout.
 */
public class DatabaseDumpTool {

    public static void dump(String filePath, List<ColumnType> schema) {
        File file = new File(filePath);
        if (!file.exists()) {
            System.err.println("Error: File not found - " + filePath);
            return;
        }

        System.out.println("===========================================================");
        System.out.println("FILE INSPECTION: " + filePath);
        System.out.println("FILE SIZE: " + file.length() + " bytes");
        System.out.println("===========================================================\n");

        try (PageManager pm = new PageManager(filePath)) {
            int numPages = pm.getNumPages();

            for (int p = 0; p < numPages; p++) {
                Page page = pm.readPage(p);
                printPageInfo(page, schema);
            }

        } catch (IOException e) {
            System.err.println("Forensic Analysis Failed: " + e.getMessage());
            e.printStackTrace();
        }
    }

    private static void printPageInfo(Page page, List<ColumnType> schema) {
        int id = page.getPageId();
        int slots = page.getSlotCount();
        int free = page.getFreeSpace();
        int used = Page.PAGE_SIZE - free;

        System.out.println("-----------------------------------------------------------");
        System.out.printf("PAGE ID: %d | Records: %d | Used: %d bytes | Free: %d bytes\n",
                id, slots, used, free);
        System.out.println("-----------------------------------------------------------");

        for (int s = 0; s < slots; s++) {
            byte[] rowBytes = page.getRow(s);

            if (rowBytes == null) {
                System.out.printf("Slot %d: [DELETED]\n", s);
                continue;
            }

            try {
                Row row = Row.deserialize(rowBytes, schema);
                System.out.printf("Slot %d:\n", s);
                System.out.println("  Content: " + row.toString());
                System.out.println("  Size:    " + rowBytes.length + " bytes");
            } catch (Exception e) {
                System.err.printf("  Slot %d: [CORRUPT DATA] %s\n", s, e.getMessage());
            }
        }
        System.out.println();
    }

    /**
     * Entry point for manual inspection.
     */
    public static void main(String[] args) {
        // Change these to inspect different files
        String targetFile = "phase1_verify.db";
        List<ColumnType> targetSchema = List.of(ColumnType.INT, ColumnType.VARCHAR);

        dump(targetFile, targetSchema);
    }
}
