package minidb.txn;

import minidb.recovery.WAL;
import java.util.*;
import java.util.concurrent.atomic.AtomicLong;

/**
 * TransactionManager owns transaction lifecycle and the global timestamp/id
 * counter. It coordinates the WAL (durability) and the LockManager (isolation).
 *
 * Lifecycle:
 *   begin()  -> log BEGIN, capture snapshot timestamp
 *   commit() -> log COMMIT (durability point), release all locks
 *   abort()  -> run undo actions, log ABORT, release all locks
 */
public final class TransactionManager {
    private final WAL wal;
    private final LockManager lockManager;
    private final AtomicLong nextId = new AtomicLong(1);
    private final Map<Long, Transaction> active = new HashMap<>();

    public TransactionManager(WAL wal, LockManager lockManager) {
        this.wal = wal;
        this.lockManager = lockManager;
    }

    public LockManager locks() { return lockManager; }

    public synchronized Transaction begin() {
        Transaction t = new Transaction(nextId.getAndIncrement());
        t.snapshotTs = t.id; // monotonic ids double as timestamps
        active.put(t.id, t);
        if (wal != null) wal.logBegin(t.id);
        return t;
    }

    public synchronized void commit(Transaction t) {
        if (t.state != Transaction.State.ACTIVE) return;
        if (wal != null) wal.logCommit(t.id);   // durable commit point
        t.state = Transaction.State.COMMITTED;
        lockManager.releaseAll(t);
        active.remove(t.id);
    }

    public synchronized void abort(Transaction t) {
        if (t.state != Transaction.State.ACTIVE) return;
        // undo in reverse order
        for (int i = t.undoActions.size() - 1; i >= 0; i--) t.undoActions.get(i).run();
        if (wal != null) wal.logAbort(t.id);
        t.state = Transaction.State.ABORTED;
        lockManager.releaseAll(t);
        active.remove(t.id);
    }

    public Collection<Transaction> activeTxns() { return active.values(); }
}
