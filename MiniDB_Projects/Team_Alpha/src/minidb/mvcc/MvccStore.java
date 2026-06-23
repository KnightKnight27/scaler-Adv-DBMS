package minidb.mvcc;

import minidb.common.Types.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicLong;

/**
 * ============================ EXTENSION TRACK B ============================
 * Multi-Version Concurrency Control (MVCC) with Snapshot Isolation.
 *
 * The core engine uses Two-Phase Locking: readers and writers block each other.
 * MVCC removes that contention by keeping MULTIPLE VERSIONS of each row. Each
 * version is stamped with the transaction that created it (beginTs) and the one
 * that superseded/deleted it (endTs). A transaction reads a consistent SNAPSHOT
 * as of its start timestamp, so:
 *
 *     READERS NEVER BLOCK WRITERS, AND WRITERS NEVER BLOCK READERS.
 *
 * VISIBILITY RULE — a version v is visible to a transaction with snapshot Ts iff:
 *     v.beginTs <= Ts                         (created at or before our snapshot)
 *   AND
 *     (v.endTs > Ts)                          (not yet superseded as of our snapshot)
 *   AND the creating txn is committed (or is ourselves).
 *
 * WRITE CONFLICT: if two concurrent transactions update the same row, the second
 * committer is aborted (first-committer-wins) — the standard SI conflict rule.
 *
 * This MvccStore is a self-contained, in-memory table used by the MVCC demo and
 * benchmark, sitting alongside the row-store. That keeps the visibility logic
 * crisp and easy to defend in the viva without entangling it with the on-disk
 * page format.
 */
public final class MvccStore {

    /** One physical version of a logical row, forming a newest-first chain. */
    private static final class Version {
        final Object[] values;
        final long beginTs;
        volatile long endTs = Long.MAX_VALUE;
        volatile boolean committed = false;
        boolean deleted = false;
        volatile Version prev;   // older version
        Version(Object[] values, long beginTs) { this.values = values; this.beginTs = beginTs; }
    }

    private final Map<Integer, Version> head = new java.util.concurrent.ConcurrentHashMap<>();
    private final AtomicLong clock = new AtomicLong(1);
    // key -> commit timestamp of the most recent committer (for SI conflict checks)
    private final Map<Integer, Long> committedAt = new java.util.concurrent.ConcurrentHashMap<>();

    public long beginTxn() { return clock.getAndIncrement(); }

    /** A transaction's read snapshot is simply its start timestamp. */
    public long snapshot(long txnTs) { return txnTs; }

    public static final class WriteConflict extends RuntimeException {
        public WriteConflict(String m) { super(m); }
    }

    /** Insert a new row version under the given transaction. */
    public synchronized void insert(long txnTs, int key, Object[] values) {
        Version v = new Version(values, txnTs);
        v.prev = head.get(key);
        head.put(key, v);
        writeSet(txnTs).add(key);
    }

    /** Update = create a new version that supersedes the prior head. The SI
     *  write-write conflict is checked at COMMIT time, not here. */
    public synchronized void update(long txnTs, int key, Object[] values) {
        Version cur = head.get(key);
        Version v = new Version(values, txnTs);
        v.prev = cur;
        if (cur != null) cur.endTs = txnTs;
        head.put(key, v);
        writeSet(txnTs).add(key);
    }

    public synchronized void delete(long txnTs, int key) {
        Version cur = head.get(key);
        if (cur == null) return;
        Version v = new Version(cur.values, txnTs);
        v.deleted = true;
        v.prev = cur;
        cur.endTs = txnTs;
        head.put(key, v);
        writeSet(txnTs).add(key);
    }

    /**
     * Commit under Snapshot Isolation with FIRST-COMMITTER-WINS:
     * if any key in this transaction's write set was committed by another
     * transaction AFTER this transaction started (committedAt > txnTs), abort.
     * Otherwise mark this txn's versions committed and stamp committedAt.
     */
    public synchronized void commit(long txnTs) {
        Set<Integer> ws = writeSets.getOrDefault(txnTs, Collections.emptySet());
        for (int key : ws) {
            Long when = committedAt.get(key);
            if (when != null && when > txnTs)
                throw new WriteConflict("write-write conflict on key " + key
                    + " (committed by a concurrent txn at ts=" + when + " after writer ts=" + txnTs + ")");
        }
        long commitTs = clock.getAndIncrement();
        for (Version h : head.values()) {
            Version v = h;
            while (v != null) {
                if (v.beginTs == txnTs) v.committed = true;
                v = v.prev;
            }
        }
        for (int key : ws) committedAt.put(key, commitTs);
        writeSets.remove(txnTs);
    }

    private final Map<Long, Set<Integer>> writeSets = new HashMap<>();
    private Set<Integer> writeSet(long txnTs) {
        return writeSets.computeIfAbsent(txnTs, k -> new HashSet<>());
    }

    /**
     * Read the version of `key` visible to a transaction with snapshot `snap`.
     * Returns null if no visible version (or the visible version is a delete).
     *
     * This method is intentionally NOT synchronized: readers traverse the
     * immutable version chain lock-free, which is the core MVCC advantage —
     * readers never block writers and never block each other.
     */
    public Object[] read(long snap, int key) {
        Version v = head.get(key);
        while (v != null) {
            boolean createdVisible = v.committed && v.beginTs <= snap;
            boolean ownVersion = v.beginTs == snap;
            if ((createdVisible || ownVersion) && v.endTs > snap) {
                return v.deleted ? null : v.values;
            }
            v = v.prev;
        }
        return null;
    }

    /** Count versions of a key — used in the demo to show the version chain. */
    public int versionCount(int key) {
        int n = 0; Version v = head.get(key);
        while (v != null) { n++; v = v.prev; }
        return n;
    }

    public Set<Integer> keys() { return new HashSet<>(head.keySet()); }
}
