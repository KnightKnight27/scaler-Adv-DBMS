package minidb.recovery;

import minidb.common.Types.*;
import minidb.sql.Catalog;
import minidb.storage.Table;
import java.util.*;

/**
 * RecoveryManager rebuilds a consistent database state from the WAL after a
 * crash, following the classic two-pass (REDO then UNDO) approach used by ARIES.
 *
 *   1. ANALYSIS: scan the log to find which transactions committed and which
 *      were still active (losers) at crash time.
 *
 *   2. REDO: replay ALL logged data operations in order, so the on-disk state
 *      reflects every change that the log recorded (repeating history).
 *
 *   3. UNDO: roll back the operations of "loser" transactions (those without a
 *      COMMIT record), restoring before-images.
 *
 * The net effect: committed transactions survive the crash; uncommitted ones
 * vanish — i.e. Atomicity + Durability.
 */
public final class RecoveryManager {
    private final WAL wal;
    private final Catalog catalog;

    public RecoveryManager(WAL wal, Catalog catalog) {
        this.wal = wal;
        this.catalog = catalog;
    }

    public String recover() {
        List<WAL.Record> log = wal.readAll();
        if (log.isEmpty()) return "No log records; nothing to recover.";

        // ---- ANALYSIS ----
        Set<Long> committed = new HashSet<>();
        Set<Long> started = new HashSet<>();
        for (WAL.Record r : log) {
            if (r.type == WAL.Type.BEGIN) started.add(r.txnId);
            if (r.type == WAL.Type.COMMIT) committed.add(r.txnId);
            if (r.type == WAL.Type.ABORT) committed.remove(r.txnId); // treated as loser/undone
        }
        Set<Long> losers = new HashSet<>(started);
        losers.removeAll(committed);

        int redos = 0, undos = 0;

        // ---- REDO (forward) ----
        for (WAL.Record r : log) {
            Table t = (r.table == null) ? null : catalog.table(r.table);
            if (t == null) continue;
            switch (r.type) {
                case INSERT:
                    if (r.after != null) { applyInsert(t, r.rid, r.after); redos++; }
                    break;
                case UPDATE:
                    if (r.after != null) { applyUpdate(t, r.rid, r.after); redos++; }
                    break;
                case DELETE:
                    applyDelete(t, r.rid); redos++;
                    break;
                default: break;
            }
        }

        // ---- UNDO (backward, losers only) ----
        for (int i = log.size() - 1; i >= 0; i--) {
            WAL.Record r = log.get(i);
            if (!losers.contains(r.txnId)) continue;
            Table t = (r.table == null) ? null : catalog.table(r.table);
            if (t == null) continue;
            switch (r.type) {
                case INSERT: applyDelete(t, r.rid); undos++; break;            // undo insert => delete
                case DELETE: if (r.before != null) { applyInsert(t, r.rid, r.before); undos++; } break;
                case UPDATE: if (r.before != null) { applyUpdate(t, r.rid, r.before); undos++; } break;
                default: break;
            }
        }

        return String.format(
            "Recovery complete. committed=%s losers=%s redos=%d undos=%d",
            committed, losers, redos, undos);
    }

    private void applyInsert(Table t, RID rid, Object[] vals) {
        Tuple tup = new Tuple(vals);
        t.redoInsert(rid, tup);
    }
    private void applyDelete(Table t, RID rid) {
        t.delete(rid, 0);
    }
    private void applyUpdate(Table t, RID rid, Object[] vals) {
        t.update(rid, new Tuple(vals), 0);
    }
}
