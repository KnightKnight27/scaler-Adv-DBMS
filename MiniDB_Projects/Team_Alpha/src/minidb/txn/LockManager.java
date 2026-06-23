package minidb.txn;

import java.util.*;

/**
 * LockManager implements Two-Phase Locking with two lock modes:
 *
 *   SHARED (S)    — multiple readers may hold it simultaneously.
 *   EXCLUSIVE (X) — only one writer; incompatible with any other lock.
 *
 * Compatibility matrix:
 *            | S        | X
 *       -----+----------+--------
 *        S   | grant    | block
 *        X   | block    | block
 *
 * Locks are taken on a "resource" identified by a string key (here: "table:RID"
 * for row-level locking, or "table" for coarse table locks).
 *
 * DEADLOCK DETECTION: we maintain a wait-for graph (txn -> txns it waits on).
 * Before a transaction blocks, we check whether granting the wait would create
 * a cycle. If so, we abort the requesting transaction (the "victim") and throw
 * DeadlockException, which the executor turns into a rollback.
 */
public final class LockManager {
    public enum Mode { SHARED, EXCLUSIVE }

    public static final class DeadlockException extends RuntimeException {
        public DeadlockException(String m) { super(m); }
    }

    private static final class LockEntry {
        final Map<Long, Mode> holders = new HashMap<>();   // txnId -> mode
        final List<long[]> waitQueue = new ArrayList<>();  // [txnId, modeOrdinal]
    }

    private final Map<String, LockEntry> table = new HashMap<>();
    // wait-for graph: who is each txn waiting on
    private final Map<Long, Set<Long>> waitsFor = new HashMap<>();

    public synchronized void acquire(Transaction txn, String resource, Mode mode) {
        // lock upgrade / re-entrancy: already hold something on this resource?
        Mode held = txn.heldLocks.get(resource);
        if (held == Mode.EXCLUSIVE) return;
        if (held == Mode.SHARED && mode == Mode.SHARED) return;

        LockEntry e = table.computeIfAbsent(resource, k -> new LockEntry());

        while (!compatible(e, txn.id, mode)) {
            // record who we are waiting for, then check for a deadlock cycle
            Set<Long> blockers = new HashSet<>(e.holders.keySet());
            blockers.remove(txn.id);
            waitsFor.put(txn.id, blockers);
            if (hasCycle(txn.id)) {
                waitsFor.remove(txn.id);
                throw new DeadlockException("Deadlock detected; aborting " + txn);
            }
            try { wait(200); } catch (InterruptedException ie) {
                Thread.currentThread().interrupt();
                throw new RuntimeException(ie);
            }
        }
        waitsFor.remove(txn.id);
        e.holders.put(txn.id, mode);
        txn.heldLocks.put(resource, mode);
    }

    private boolean compatible(LockEntry e, long txnId, Mode mode) {
        for (Map.Entry<Long, Mode> h : e.holders.entrySet()) {
            if (h.getKey() == txnId) continue; // ignore self
            if (mode == Mode.EXCLUSIVE || h.getValue() == Mode.EXCLUSIVE) return false;
        }
        return true;
    }

    /** Release every lock held by a transaction (called at commit/abort). */
    public synchronized void releaseAll(Transaction txn) {
        for (String res : txn.heldLocks.keySet()) {
            LockEntry e = table.get(res);
            if (e != null) e.holders.remove(txn.id);
        }
        txn.heldLocks.clear();
        waitsFor.remove(txn.id);
        notifyAll(); // wake any waiters
    }

    // DFS cycle detection in the wait-for graph starting from `start`.
    private boolean hasCycle(long start) {
        Set<Long> visited = new HashSet<>();
        Deque<Long> stack = new ArrayDeque<>();
        stack.push(start);
        boolean first = true;
        while (!stack.isEmpty()) {
            long cur = stack.pop();
            if (!first && cur == start) return true;
            first = false;
            if (!visited.add(cur)) continue;
            Set<Long> next = waitsFor.get(cur);
            if (next != null) for (long n : next) stack.push(n);
        }
        return false;
    }
}
