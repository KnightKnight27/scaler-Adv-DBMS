package com.minidb.recovery;

import com.minidb.storage.*;
import com.minidb.txn.Transaction;
import com.minidb.txn.TransactionManager;

import java.util.Arrays;
import java.util.List;

/**
 * NOT a test class — a standalone main() that is launched as a subprocess by
 * CrashRecoveryTest.  It:
 *   1. Opens the DB + WAL files whose paths are given as args[0] and args[1].
 *   2. Commits transaction A (inserts two rows — Alice, Bob).
 *   3. Begins transaction B, inserts Charlie, explicitly flushes the dirty page
 *      to disk so it is physically present in the .db file before the crash.
 *   4. Calls Runtime.halt(42) — a hard JVM abort — WITHOUT committing B and
 *      without calling close() on any handle.
 *
 * This guarantees:
 *   • The WAL file contains: INSERT(Alice), INSERT(Bob), COMMIT(A), INSERT(Charlie).
 *     NO COMMIT(B) record exists.
 *   • The .db page file contains all three rows written to disk (forcibly flushed).
 *
 * Recovery must therefore:
 *   • REDO A  — Alice and Bob may or may not be on disk, redo ensures they are.
 *   • UNDO B  — Charlie IS on disk (we flushed the page) but has no COMMIT;
 *               undo must remove it.
 */
public class CrashSimulator {

    public static void main(String[] args) throws Exception {
        if (args.length < 2) {
            System.err.println("Usage: CrashSimulator <dbFile> <walFile>");
            System.exit(1);
        }

        String dbFile  = args[0];
        String walFile = args[1];

        List<ColumnType> schema = Arrays.asList(ColumnType.INT, ColumnType.VARCHAR);
        WALManager         wal  = new WALManager(walFile);
        TransactionManager tm   = new TransactionManager(wal);
        PageManager        pm   = new PageManager(dbFile);

        // Use a WAL-free BufferPool so recovery does NOT auto-run on open here.
        // We pass null for walManager to skip recovery: this is the "before crash"
        // instance, not the "after crash" recovery instance.
        BufferPool bp = new BufferPool(pm, 32, null);
        HeapFile   hf = new HeapFile(bp, pm, schema, wal);

        // ── Transaction A: Alice + Bob, COMMITTED ──────────────────────────
        Transaction tA = tm.begin();
        setCurrentTxn(tm, tA);

        Row alice = new Row(new Object[]{1, "Alice"});
        alice.setXmin(tA.getId());
        hf.insert(alice);

        Row bob = new Row(new Object[]{2, "Bob"});
        bob.setXmin(tA.getId());
        hf.insert(bob);

        tm.commit(tA);   // WAL: INSERT(1,Alice) INSERT(2,Bob) COMMIT(A)  — all synced

        // ── Transaction B: Charlie, NOT committed ──────────────────────────
        Transaction tB = tm.begin();
        setCurrentTxn(tm, tB);

        Row charlie = new Row(new Object[]{3, "Charlie"});
        charlie.setXmin(tB.getId());
        hf.insert(charlie);

        // Force the dirty page to disk so it physically exists in the .db file.
        // This is the key: without this the undo phase has nothing to remove.
        bp.flushAll();
        pm.getNumPages(); // ensure file length is up-to-date (no-op read)

        // ── HARD CRASH — no commit, no close() ────────────────────────────
        // Runtime.halt() is a true hard abort: no shutdown hooks, no finalizers,
        // no try-finally cleanup anywhere in the JVM.
        Runtime.getRuntime().halt(42);
    }

    /** Reflectively set the thread-local current transaction. */
    private static void setCurrentTxn(TransactionManager tm, Transaction txn) throws Exception {
        java.lang.reflect.Field f = TransactionManager.class.getDeclaredField("currentTxn");
        f.setAccessible(true);
        @SuppressWarnings("unchecked")
        java.util.concurrent.ConcurrentHashMap<?,?> ignored = null; // just to keep import
        ThreadLocal<Transaction> tl = (ThreadLocal<Transaction>) f.get(tm);
        tl.set(txn);
    }
}
