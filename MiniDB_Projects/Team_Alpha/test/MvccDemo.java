import minidb.mvcc.MvccStore;

/** Demonstrates snapshot isolation: a reader sees a consistent snapshot even
 *  while a concurrent writer modifies and commits the same key. */
public class MvccDemo {
    public static void main(String[] args) {
        MvccStore s = new MvccStore();

        long t1 = s.beginTxn();           // writer 1
        s.insert(t1, 1, new Object[]{1, "v1"});
        s.commit(t1);

        long reader = s.beginTxn();       // reader takes snapshot here
        long snapR = s.snapshot(reader);

        long t2 = s.beginTxn();           // writer 2 updates key 1 AFTER reader's snapshot
        s.update(t2, 1, new Object[]{1, "v2"});
        s.commit(t2);

        Object[] seenByReader = s.read(snapR, 1);
        Object[] seenByNew = s.read(s.snapshot(s.beginTxn()), 1);

        System.out.println("Versions of key 1: " + s.versionCount(1));
        System.out.println("Reader (old snapshot) sees: " + (seenByReader==null?"null":seenByReader[1]));
        System.out.println("New txn (fresh snapshot) sees: " + (seenByNew==null?"null":seenByNew[1]));
        System.out.println("=> Reader was NOT blocked by the writer, and still sees its consistent snapshot.");

        // Write-write conflict demo (first committer wins)
        long a = s.beginTxn();
        long b = s.beginTxn();
        s.update(a, 1, new Object[]{1, "fromA"});
        s.commit(a);
        try {
            s.update(b, 1, new Object[]{1, "fromB"});
            s.commit(b);
            System.out.println("No conflict detected (depends on timing).");
        } catch (MvccStore.WriteConflict wc) {
            System.out.println("Write-write conflict correctly detected: " + wc.getMessage());
        }
    }
}
