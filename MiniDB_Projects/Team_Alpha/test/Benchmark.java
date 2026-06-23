import minidb.*;
import minidb.mvcc.MvccStore;
import java.io.*;
import java.util.concurrent.locks.ReentrantReadWriteLock;

/**
 * Benchmark suite for the MiniDB report.
 *   (1) Index scan vs sequential scan latency as table size grows
 *   (2) Buffer pool hit rate under repeated access
 *   (3) MVCC vs 2PL read throughput under write contention
 *
 * Each benchmark prints a small table of numbers used in README section 10.
 */
public class Benchmark {
    public static void main(String[] args) throws Exception {
        System.out.println("=================  MiniDB BENCHMARKS  =================\n");
        benchIndexVsScan();
        benchBufferPool();
        benchMvccVs2pl();
    }

    // ---- (1) index scan vs seq scan ----
    static void benchIndexVsScan() throws Exception {
        System.out.println("## Benchmark 1: Index Scan vs Sequential Scan (point lookup)");
        int[] sizes = {2000, 10000, 40000};
        System.out.printf("%-10s %-20s %-20s %-10s%n", "rows", "seqScan(us/query)", "idxScan(us/query)", "speedup");
        for (int n : sizes) {
            String dir = "bench_idx_" + n;
            deleteDir(dir);
            MiniDB db = new MiniDB(dir);
            db.exec("CREATE TABLE t (id INT PRIMARY KEY, val INT)");
            for (int i = 1; i <= n; i++) db.exec("INSERT INTO t VALUES (" + i + ", " + (i*7%1000) + ")");
            db.executor().showPlan = false;
            int reps = 200, target = n / 2;

            // sequential scan: equality on NON-indexed column 'val' -> full scan
            long s0 = System.nanoTime();
            for (int r = 0; r < reps; r++) db.exec("SELECT id FROM t WHERE val = 12345");
            long seqUs = (System.nanoTime() - s0) / 1000 / reps;

            // index scan: equality on indexed PK 'id' -> B+ tree point lookup
            long s1 = System.nanoTime();
            for (int r = 0; r < reps; r++) db.exec("SELECT id FROM t WHERE id = " + target);
            long idxUs = (System.nanoTime() - s1) / 1000 / reps;

            System.out.printf("%-10d %-20d %-20d %-10s%n", n, seqUs, idxUs,
                String.format("%.1fx", seqUs / (double) Math.max(1, idxUs)));
            db.shutdown();
            deleteDir(dir);
        }
        System.out.println("Index point lookup is ~constant (O(log n)); seq scan grows linearly with rows.\n");
    }

    // ---- (2) buffer pool ----
    static void benchBufferPool() throws Exception {
        System.out.println("## Benchmark 2: Buffer Pool Hit Rate");
        String dir = "bench_bp";
        deleteDir(dir);
        MiniDB db = new MiniDB(dir);
        db.exec("CREATE TABLE t (id INT PRIMARY KEY, val INT)");
        for (int i = 1; i <= 5000; i++) db.exec("INSERT INTO t VALUES (" + i + ", " + i + ")");
        long hb=0, mb=0;
        for (var t : db.catalog().allTables()) { hb += t.pool().hits; mb += t.pool().misses; }
        for (int r = 0; r < 50; r++) db.exec("SELECT id FROM t WHERE val = 1"); // full scans, reuse cached pages
        long ha=0, ma=0;
        for (var t : db.catalog().allTables()) { ha += t.pool().hits; ma += t.pool().misses; }
        long hits = ha - hb, misses = ma - mb;
        double rate = 100.0 * hits / Math.max(1, hits + misses);
        System.out.printf("Read-only scan phase: hits=%d misses=%d hitRate=%.1f%%%n", hits, misses, rate);
        System.out.println("Hot pages stay cached, so repeated scans hit the buffer pool instead of disk.\n");
        db.shutdown();
        deleteDir(dir);
    }

    // ---- (3) MVCC vs 2PL read throughput under contention ----
    // Both schemes share the SAME workload: one writer that holds a row's
    // write privilege for a fixed work-unit, and one reader doing many reads.
    // 2PL  -> reader must take a shared lock, blocked whenever writer holds X.
    // MVCC -> reader uses a snapshot, never blocked.
    static void benchMvccVs2pl() throws Exception {
        System.out.println("## Benchmark 3: MVCC vs 2PL Reader Blocking Under Write Contention");
        final int WRITES = 200;
        final int READS = 200;
        // Single HOT row that a writer updates with a non-trivial hold time while
        // a reader repeatedly reads the SAME row. This is the worst case for 2PL
        // (reader must wait out every writer hold) and the best case for MVCC.

        // ---------- 2PL ----------
        final ReentrantReadWriteLock lock = new ReentrantReadWriteLock();
        final int[] data = {0};
        final long[] blocked2 = {0};
        Thread w2 = new Thread(() -> {
            for (int i = 0; i < WRITES; i++) {
                lock.writeLock().lock();
                try { data[0]++; holdWork(); } finally { lock.writeLock().unlock(); }
            }
        });
        Thread r2 = new Thread(() -> {
            for (int i = 0; i < READS; i++) {
                long want = System.nanoTime();
                lock.readLock().lock();
                long got = System.nanoTime();
                try { blocked2[0] += (got - want); int v = data[0]; if (v < 0) System.out.print(""); }
                finally { lock.readLock().unlock(); }
                try { Thread.sleep(0, 200_000); } catch (InterruptedException ignored) {}
            }
        });
        w2.start(); r2.start(); w2.join(); r2.join();

        // ---------- MVCC ----------
        MvccStore mv = new MvccStore();
        long init = mv.beginTxn();
        mv.insert(init, 0, new Object[]{0, 0});
        mv.commit(init);
        final long[] blockedM = {0};
        Thread wM = new Thread(() -> {
            for (int i = 0; i < WRITES; i++) {
                long tx = mv.beginTxn();
                mv.update(tx, 0, new Object[]{0, i});
                holdWork();
                try { mv.commit(tx); } catch (RuntimeException ignored) {}
            }
        });
        Thread rM = new Thread(() -> {
            long snap = mv.snapshot(mv.beginTxn());
            for (int i = 0; i < READS; i++) {
                long want = System.nanoTime();
                Object[] v = mv.read(snap, 0);   // never blocks on the writer
                long got = System.nanoTime();
                blockedM[0] += (got - want);
                if (v != null && ((Integer) v[1]) < 0) System.out.print("");
                try { Thread.sleep(0, 200_000); } catch (InterruptedException ignored) {}
            }
        });
        wM.start(); rM.start(); wM.join(); rM.join();

        double avg2 = blocked2[0] / 1000.0 / READS;
        double avgM = blockedM[0] / 1000.0 / READS;
        System.out.printf("(1 writer with ~%dus lock hold + 1 reader, single hot row)%n", 50);
        System.out.printf("%-12s %-30s%n", "scheme", "avg read lock-acquire wait (us)");
        System.out.printf("%-12s %-30.2f%n", "2PL",  avg2);
        System.out.printf("%-12s %-30.2f%n", "MVCC", avgM);
        System.out.printf("Reader wait reduction with MVCC: %.1fx less waiting per read%n",
            avg2 / Math.max(0.001, avgM));
        System.out.println("2PL readers block until the writer releases the exclusive lock on the hot row;");
        System.out.println("MVCC readers see a consistent snapshot immediately and never wait.\n");
    }

    // ~50 microseconds of work to simulate a meaningful write/lock hold
    static void holdWork() {
        long end = System.nanoTime() + 50_000;
        double x = 0;
        while (System.nanoTime() < end) x += Math.sqrt(x + 1);
        if (x < 0) System.out.print("");
    }

    // a small fixed unit of CPU work to simulate holding a lock / doing a write
    static void work() { double x = 0; for (int j = 0; j < 300; j++) x += Math.sqrt(j); if (x < 0) System.out.print(""); }

    static void deleteDir(String d) {
        File f = new File(d);
        if (f.exists()) { File[] cs = f.listFiles(); if (cs != null) for (File c : cs) c.delete(); f.delete(); }
    }
}
