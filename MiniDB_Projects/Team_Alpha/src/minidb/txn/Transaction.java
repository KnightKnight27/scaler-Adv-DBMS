package minidb.txn;

import java.util.*;

/**
 * A transaction's runtime state.
 *
 * Under Strict Two-Phase Locking (the isolation mechanism for our core engine),
 * a transaction acquires locks as it runs (growing phase) and releases them ALL
 * AT ONCE at commit/abort (shrinking phase). Holding write locks until commit is
 * what gives us serializable, recoverable schedules.
 */
public final class Transaction {
    public enum State { ACTIVE, COMMITTED, ABORTED }

    public final long id;
    public State state = State.ACTIVE;

    // locks currently held: key string -> lock mode
    public final Map<String, LockManager.Mode> heldLocks = new HashMap<>();

    // undo information for rollback (filled by the executor)
    public final List<Runnable> undoActions = new ArrayList<>();

    // MVCC: snapshot timestamp captured at txn start (used only by MVCC extension)
    public long snapshotTs;

    public Transaction(long id) { this.id = id; }

    @Override public String toString() { return "Txn#" + id + "(" + state + ")"; }
}
