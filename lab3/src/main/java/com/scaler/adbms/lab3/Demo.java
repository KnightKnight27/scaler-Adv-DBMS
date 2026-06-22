package com.scaler.adbms.lab3;

/**
 * Hand-runnable walkthrough split into two parts.
 *
 * <p>Part A drives the {@link ClockSweepReplacer} directly so the
 * "second chance" mechanic is visible step-by-step.
 *
 * <p>Part B exercises a {@link BufferPoolManager} end-to-end: allocating
 * pages, forcing evictions, writing dirty pages back to disk, and
 * reloading evicted pages on a miss.
 */
public final class Demo {

    public static void main(String[] args) {
        System.out.println("=== ADBMS Lab 3 :: Clock-Sweep Buffer Pool ===\n");
        partA_ReplacerOnly();
        partB_BufferPoolEndToEnd();
    }

    // ------------------------------------------------------------------
    // Part A: just the replacer
    // ------------------------------------------------------------------

    private static void partA_ReplacerOnly() {
        System.out.println("--- Part A: clock-sweep replacer in isolation ---");
        ClockSweepReplacer r = new ClockSweepReplacer(4);

        // Mark all four frames evictable. We intentionally do NOT call
        // recordAccess yet, so every ref bit starts at 0.
        for (int i = 0; i < 4; i++) r.setEvictable(i, true);
        System.out.println("4 frames evictable, all ref bits = 0");
        System.out.println("  victim -> " + r.pickVictim().orElse(-1)); // 0
        System.out.println("  victim -> " + r.pickVictim().orElse(-1)); // 1

        // Put frames 0 and 1 back, then simulate access on frame 2.
        // Hand is now parked at frame 2.
        r.setEvictable(0, true);
        r.setEvictable(1, true);
        r.recordAccess(2);  // ref bit on frame 2 only

        System.out.println("\nReinstated frames 0,1 + ref bit set on frame 2");
        System.out.println("  victim -> " + r.pickVictim().orElse(-1));
        System.out.println("    ^ frame 2 was spared: ref=1 was cleared, hand moved on");
        System.out.println("  victim -> " + r.pickVictim().orElse(-1));
        System.out.println("    ^ wrap-around, frame 2 is now ref=0 like the rest\n");
    }

    // ------------------------------------------------------------------
    // Part B: full buffer pool
    // ------------------------------------------------------------------

    private static void partB_BufferPoolEndToEnd() {
        System.out.println("--- Part B: buffer pool with disk-backed pages ---");

        try (DiskManager disk = new DiskManager()) {
            BufferPoolManager bpm = new BufferPoolManager(3, disk);
            System.out.println("Pool size: " + bpm.poolSize());

            // Fill the pool.
            int[] pids = new int[5];
            for (int i = 0; i < 3; i++) {
                Page p = bpm.newPage();
                pids[i] = p.getPageId();
                p.writeString("page-" + pids[i] + " :: hello");
                bpm.unpinPage(pids[i], true);
                System.out.println("  wrote " + p);
            }
            System.out.println("After fill: " + bpm.stats());

            // Force two evictions by allocating two more pages.
            for (int i = 3; i < 5; i++) {
                Page p = bpm.newPage();
                pids[i] = p.getPageId();
                p.writeString("page-" + pids[i] + " :: late arrival");
                bpm.unpinPage(pids[i], true);
                System.out.println("  allocated " + p
                    + " (total evictions so far: " + bpm.stats().evictions() + ")");
            }

            // Re-fetch an evicted page -> miss, reload from disk.
            Page reloaded = bpm.fetchPage(pids[0]);
            System.out.println("\nReloaded page " + pids[0] + " from disk:");
            System.out.println("  contents = '" + reloaded.readString() + "'");
            bpm.unpinPage(pids[0], false);

            // Re-fetch one currently cached -> hit.
            Page hit = bpm.fetchPage(pids[4]);
            System.out.println("Hit on page " + pids[4]
                + ": '" + hit.readString() + "'");
            bpm.unpinPage(pids[4], false);

            bpm.flushAll();
            BufferPoolStats s = bpm.stats();
            System.out.println("\nFinal stats: " + s);
            System.out.printf("Hit rate: %.2f%%%n", s.hitRate() * 100);
        }
    }

    private Demo() {}
}
