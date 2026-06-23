import minidb.txn.*;

/**
 * Demonstrates deadlock detection under Two-Phase Locking.
 *
 * Two transactions acquire two resources in OPPOSITE order:
 *   T1: lock A, then try to lock B
 *   T2: lock B, then try to lock A
 * This is the classic deadlock. The LockManager's wait-for graph cycle
 * detection should catch it and abort one transaction (the victim) with a
 * DeadlockException.
 */
public class DeadlockDemo {
    public static void main(String[] args) throws Exception {
        LockManager lm = new LockManager();
        TransactionManager tm = new TransactionManager(null, lm);

        Transaction t1 = tm.begin();
        Transaction t2 = tm.begin();

        final String A = "T:accounts", B = "T:orders";
        final boolean[] victim = {false};

        // T1 grabs A first
        lm.acquire(t1, A, LockManager.Mode.EXCLUSIVE);
        // T2 grabs B first
        lm.acquire(t2, B, LockManager.Mode.EXCLUSIVE);
        System.out.println("T1 holds " + A + ", T2 holds " + B);

        // Now each tries to grab the other's resource, in separate threads.
        Thread th1 = new Thread(() -> {
            try {
                System.out.println("T1 requests " + B + " ...");
                lm.acquire(t1, B, LockManager.Mode.EXCLUSIVE);
                System.out.println("T1 acquired " + B);
            } catch (LockManager.DeadlockException e) {
                System.out.println("T1 ABORTED by deadlock detector: " + e.getMessage());
                victim[0] = true;
                lm.releaseAll(t1);
            }
        });
        Thread th2 = new Thread(() -> {
            try {
                System.out.println("T2 requests " + A + " ...");
                lm.acquire(t2, A, LockManager.Mode.EXCLUSIVE);
                System.out.println("T2 acquired " + A);
            } catch (LockManager.DeadlockException e) {
                System.out.println("T2 ABORTED by deadlock detector: " + e.getMessage());
                victim[0] = true;
                lm.releaseAll(t2);
            }
        });
        th1.start(); Thread.sleep(50); th2.start();
        th1.join(3000); th2.join(3000);

        System.out.println(victim[0]
            ? "=> Deadlock was detected and broken by aborting a victim transaction."
            : "=> NO deadlock detected (unexpected).");
    }
}
