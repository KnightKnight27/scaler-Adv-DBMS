package com.minidb.storage;

import com.minidb.recovery.WALManager;
import java.io.IOException;
import java.util.AbstractMap;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * The table abstraction representing a collection of pages stored in a file.
 */
public class HeapFile {
    private final BufferPool bufferPool;
    private final PageManager pageManager;
    private final List<ColumnType> schema;

    private final WALManager walManager;

    public HeapFile(BufferPool bufferPool, PageManager pageManager, List<ColumnType> schema) {
        this(bufferPool, pageManager, schema, null);
    }

    public HeapFile(BufferPool bufferPool, PageManager pageManager, List<ColumnType> schema, WALManager walManager) {
        this.bufferPool = bufferPool;
        this.pageManager = pageManager;
        this.schema = schema;
        this.walManager = walManager;
    }

    /**
     * Inserts a row into the table.
     * Searches for a page with enough space or allocates a new one.
     */
    public RowId insert(Row row) throws IOException {
        byte[] rowBytes = Row.serialize(row, schema);
        int numPages = pageManager.getNumPages();
        
        Page targetPage = null;
        int slotNumber = -1;

        // Scan existing pages for space
        for (int i = 0; i < numPages; i++) {
            Page page = bufferPool.getPage(i);
            if (page.getFreeSpace() >= Page.SLOT_SIZE + rowBytes.length) {
                targetPage = page;
                slotNumber = page.insertRow(rowBytes);
                
                // Log INSERT after slot is known but image already final
                if (walManager != null) {
                    com.minidb.recovery.LogRecord record = new com.minidb.recovery.LogRecord(
                            row.getXmin(), targetPage.getPageId(), slotNumber, null, rowBytes,
                            com.minidb.recovery.LogRecord.Type.INSERT);
                    walManager.appendLogRecord(record);
                }
                break;
            }
        }

        // No space in existing pages, allocate new one
        if (targetPage == null) {
            targetPage = pageManager.allocatePage();
            bufferPool.addPage(targetPage);
            slotNumber = targetPage.insertRow(rowBytes);
            
            // Log INSERT after slot is known
            if (walManager != null) {
                com.minidb.recovery.LogRecord record = new com.minidb.recovery.LogRecord(
                        row.getXmin(), targetPage.getPageId(), slotNumber, null, rowBytes,
                        com.minidb.recovery.LogRecord.Type.INSERT);
                walManager.appendLogRecord(record);
            }
        }

        bufferPool.markDirty(targetPage.getPageId());
        return new RowId(targetPage.getPageId(), slotNumber);
    }

    public Row get(RowId rowId) throws IOException {
        Page page = bufferPool.getPage(rowId.pageId());
        byte[] bytes = page.getRow(rowId.slotNumber());
        if (bytes == null) return null;
        return Row.deserialize(bytes, schema);
    }

    public void delete(RowId rowId) throws IOException {
        Page page = bufferPool.getPage(rowId.pageId());
        
        if (walManager != null) {
            byte[] oldBytes = page.getRow(rowId.slotNumber());
            com.minidb.recovery.LogRecord record = new com.minidb.recovery.LogRecord(
                    0L, rowId.pageId(), rowId.slotNumber(), oldBytes, null,
                    com.minidb.recovery.LogRecord.Type.DELETE);
            walManager.appendLogRecord(record);
        }
        
        page.deleteRow(rowId.slotNumber());
        bufferPool.markDirty(rowId.pageId());
    }

    public List<Row> scan() throws IOException {
        List<Row> results = new ArrayList<>();
        int numPages = pageManager.getNumPages();
        for (int i = 0; i < numPages; i++) {
            Page page = bufferPool.getPage(i);
            for (int s = 0; s < page.getSlotCount(); s++) {
                byte[] bytes = page.getRow(s);
                if (bytes != null) {
                    results.add(Row.deserialize(bytes, schema));
                }
            }
        }
        return results;
    }

    public List<Map.Entry<RowId, Row>> scanWithIds() throws IOException {
        List<Map.Entry<RowId, Row>> results = new ArrayList<>();
        int numPages = pageManager.getNumPages();
        for (int i = 0; i < numPages; i++) {
            Page page = bufferPool.getPage(i);
            for (int s = 0; s < page.getSlotCount(); s++) {
                byte[] bytes = page.getRow(s);
                if (bytes != null) {
                    RowId rid = new RowId(i, s);
                    Row row = Row.deserialize(bytes, schema);
                    results.add(new AbstractMap.SimpleEntry<>(rid, row));
                }
            }
        }
        return results;
    }

    public void update(RowId rowId, Row row) throws IOException {
        Page page = bufferPool.getPage(rowId.pageId());
        byte[] rowBytes = Row.serialize(row, schema);
        
        if (walManager != null) {
            byte[] oldBytes = page.getRow(rowId.slotNumber());
            com.minidb.recovery.LogRecord record = new com.minidb.recovery.LogRecord(
                    row.getXmax(), rowId.pageId(), rowId.slotNumber(), oldBytes, rowBytes,
                    com.minidb.recovery.LogRecord.Type.UPDATE);
            walManager.appendLogRecord(record);
        }
        
        if (!page.updateRow(rowId.slotNumber(), rowBytes)) {
            throw new IOException("In-place update failed. Row sizes must match exactly.");
        }
        bufferPool.markDirty(rowId.pageId());
    }

    public java.util.Iterator<Row> iterator() {
        return new java.util.Iterator<Row>() {
            private int currentPageId = 0;
            private int currentSlot = 0;
            private Row nextRow = null;

            @Override
            public boolean hasNext() {
                if (nextRow != null) return true;
                try {
                    int numPages = pageManager.getNumPages();
                    while (currentPageId < numPages) {
                        Page page = bufferPool.getPage(currentPageId);
                        while (currentSlot < page.getSlotCount()) {
                            byte[] bytes = page.getRow(currentSlot);
                            if (bytes != null) {
                                nextRow = Row.deserialize(bytes, schema);
                                nextRow.setRowId(new RowId(currentPageId, currentSlot));
                                currentSlot++;
                                return true;
                            }
                            currentSlot++;
                        }
                        currentPageId++;
                        currentSlot = 0;
                    }
                } catch (IOException e) {
                    throw new RuntimeException(e);
                }
                return false;
            }

            @Override
            public Row next() {
                if (!hasNext()) throw new java.util.NoSuchElementException();
                Row row = nextRow;
                nextRow = null;
                return row;
            }
        };
    }
}
