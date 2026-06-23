package com.minidb.recovery;

import com.minidb.storage.Page;
import com.minidb.storage.PageManager;

import java.io.IOException;
import java.util.*;
import java.util.logging.Logger;

/**
 * Performs crash recovery using the Write-Ahead Log (WAL).
 *
 * Recovery runs in two phases on startup when an existing WAL log is present:
 *
 *   REDO phase  – re-applies the after-image of every INSERT/UPDATE/DELETE record
 *                 that belongs to a *committed* transaction, in LSN order.
 *                 This covers changes that were in the WAL but whose pages had not
 *                 yet been flushed to disk before the crash.
 *
 *   UNDO phase  – removes the effects of every INSERT/UPDATE/DELETE record that
 *                 belongs to a transaction with NO COMMIT record, processing records
 *                 in *reverse* LSN order.  The before-image is applied for
 *                 UPDATE/DELETE; for INSERT the slot is marked deleted.
 */
public class RecoveryManager {

    private static final Logger LOG = Logger.getLogger(RecoveryManager.class.getName());

    private final WALManager walManager;
    private final PageManager pageManager;

    public RecoveryManager(WALManager walManager, PageManager pageManager) {
        this.walManager  = walManager;
        this.pageManager = pageManager;
    }

    /**
     * Entry point: reads the full WAL and performs redo + undo.
     *
     * @return number of pages that were modified during recovery
     */
    public int recover() throws IOException {
        List<LogRecord> allRecords = walManager.readAllRecords();
        if (allRecords.isEmpty()) {
            LOG.info("RecoveryManager: WAL is empty, nothing to recover.");
            return 0;
        }

        // --- Classify transactions ---
        Set<Long> committedTxns = new HashSet<>();
        for (LogRecord r : allRecords) {
            if (r.getType() == LogRecord.Type.COMMIT) {
                committedTxns.add(r.getTxnId());
            }
        }

        // Collect data records for each txn
        Map<Long, List<LogRecord>> dataByTxn = new LinkedHashMap<>();
        for (LogRecord r : allRecords) {
            if (isDataRecord(r)) {
                dataByTxn.computeIfAbsent(r.getTxnId(), k -> new ArrayList<>()).add(r);
            }
        }

        Set<Long> uncommittedTxns = new HashSet<>(dataByTxn.keySet());
        uncommittedTxns.removeAll(committedTxns);

        LOG.info(String.format("RecoveryManager: %d committed txns to redo, %d uncommitted txns to undo.",
                committedTxns.size(), uncommittedTxns.size()));

        // Cache of pages we load/modify during recovery
        Map<Integer, Page> pageCache = new HashMap<>();
        int totalNumPages = pageManager.getNumPages();

        // ---------------------------------------------------------------
        // REDO phase — forward scan, committed txns only
        // ---------------------------------------------------------------
        for (LogRecord r : allRecords) {
            if (!isDataRecord(r)) continue;
            if (!committedTxns.contains(r.getTxnId())) continue;

            Page page = getOrLoadPage(pageCache, r.getPageId(), totalNumPages);
            if (page == null) {
                // Page does not exist yet on disk – need to extend the file.
                // Allocate up to and including this pageId.
                while (pageManager.getNumPages() <= r.getPageId()) {
                    pageManager.allocatePage();
                }
                totalNumPages = pageManager.getNumPages();
                page = getOrLoadPage(pageCache, r.getPageId(), totalNumPages);
            }

            applyRedo(page, r);
        }

        // ---------------------------------------------------------------
        // UNDO phase — reverse scan, uncommitted txns only
        // ---------------------------------------------------------------
        List<LogRecord> reversed = new ArrayList<>(allRecords);
        Collections.reverse(reversed);

        for (LogRecord r : reversed) {
            if (!isDataRecord(r)) continue;
            if (!uncommittedTxns.contains(r.getTxnId())) continue;

            Page page = getOrLoadPage(pageCache, r.getPageId(), totalNumPages);
            if (page == null) continue;  // page doesn't exist; nothing to undo

            applyUndo(page, r);
        }

        // ---------------------------------------------------------------
        // Flush all modified pages back to disk
        // ---------------------------------------------------------------
        int flushed = 0;
        for (Page page : pageCache.values()) {
            pageManager.writePage(page);
            flushed++;
        }

        LOG.info("RecoveryManager: recovery complete. Flushed " + flushed + " pages.");
        return flushed;
    }

    // ------------------------------------------------------------------
    // Redo helpers
    // ------------------------------------------------------------------

    private void applyRedo(Page page, LogRecord r) {
        switch (r.getType()) {
            case INSERT -> {
                // Re-insert the row at exactly the logged slot number.
                // If the slot already has data (page was flushed before crash), skip.
                int slot = r.getSlotNumber();
                if (slot >= 0 && slot < page.getSlotCount()) {
                    byte[] existing = page.getRow(slot);
                    if (existing != null) {
                        // Slot already has data — redo already applied (idempotent).
                        return;
                    }
                }
                // Insert the afterImage; slot assignment may differ but we accept it.
                page.insertRow(r.getAfterImage());
            }
            case UPDATE -> {
                // Apply afterImage to the specific slot.
                int slot = r.getSlotNumber();
                if (slot >= 0 && r.getAfterImage() != null) {
                    page.updateRow(slot, r.getAfterImage());
                }
            }
            case DELETE -> {
                // Mark the slot as deleted.
                int slot = r.getSlotNumber();
                if (slot >= 0) {
                    page.deleteRow(slot);
                }
            }
            default -> { /* COMMIT/ABORT — not a data record, ignore */ }
        }
    }

    // ------------------------------------------------------------------
    // Undo helpers
    // ------------------------------------------------------------------

    private void applyUndo(Page page, LogRecord r) {
        switch (r.getType()) {
            case INSERT -> {
                // Undo an insert: mark the slot as deleted.
                int slot = r.getSlotNumber();
                if (slot >= 0) {
                    page.deleteRow(slot);
                }
            }
            case UPDATE -> {
                // Undo an update: restore the beforeImage.
                int slot = r.getSlotNumber();
                if (slot >= 0 && r.getBeforeImage() != null) {
                    page.updateRow(slot, r.getBeforeImage());
                }
            }
            case DELETE -> {
                // Undo a delete: re-insert the beforeImage at the slot.
                int slot = r.getSlotNumber();
                if (slot >= 0 && r.getBeforeImage() != null) {
                    // Try in-place restore first (if slot is still there but marked deleted)
                    boolean restored = page.restoreRow(slot, r.getBeforeImage());
                    if (!restored) {
                        // Slot gone — just re-insert (best effort)
                        page.insertRow(r.getBeforeImage());
                    }
                }
            }
            default -> { /* ignore */ }
        }
    }

    // ------------------------------------------------------------------
    // Utility
    // ------------------------------------------------------------------

    private boolean isDataRecord(LogRecord r) {
        return r.getType() == LogRecord.Type.INSERT
            || r.getType() == LogRecord.Type.UPDATE
            || r.getType() == LogRecord.Type.DELETE;
    }

    private Page getOrLoadPage(Map<Integer, Page> cache, int pageId, int numPages) throws IOException {
        if (cache.containsKey(pageId)) return cache.get(pageId);
        if (pageId < 0 || pageId >= numPages) return null;
        Page page = pageManager.readPage(pageId);
        cache.put(pageId, page);
        return page;
    }
}
