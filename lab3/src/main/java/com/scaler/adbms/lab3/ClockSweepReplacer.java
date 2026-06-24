package com.scaler.adbms.lab3;

import java.util.Optional;

/**
 * Clock-sweep (second-chance) page replacement.
 *
 * <p>Each frame slot tracks two bits:
 * <ul>
 *   <li><b>evictable</b> — only evictable frames are eviction candidates</li>
 *   <li><b>refBit</b> — set when the page is accessed. On a sweep a set
 *       refBit is cleared instead of evicting (the "second chance").</li>
 * </ul>
 *
 * <p>{@link #pickVictim()} advances a clock hand forward. For each
 * evictable frame:
 * <pre>
 *   refBit == 1  -> clear it, advance (spared this round)
 *   refBit == 0  -> evict, return frame id
 * </pre>
 * Non-evictable (pinned) frames are skipped. If no frame is evictable,
 * pickVictim returns {@link Optional#empty()}.
 */
public final class ClockSweepReplacer {
    private final int numFrames;
    private final boolean[] refBit;
    private final boolean[] evictable;
    private int hand = 0;
    private int evictableCount = 0;
    private final Object lock = new Object();

    public ClockSweepReplacer(int numFrames) {
        if (numFrames <= 0) {
            throw new IllegalArgumentException("numFrames must be > 0");
        }
        this.numFrames = numFrames;
        this.refBit = new boolean[numFrames];
        this.evictable = new boolean[numFrames];
    }

    public int size() {
        synchronized (lock) {
            return evictableCount;
        }
    }

    /** Called whenever a page in {@code frameId} is touched. */
    public void recordAccess(int frameId) {
        checkRange(frameId);
        synchronized (lock) {
            refBit[frameId] = true;
        }
    }

    /** Pin (false) and unpin-to-zero (true) flip this flag. */
    public void setEvictable(int frameId, boolean isEvictable) {
        checkRange(frameId);
        synchronized (lock) {
            boolean was = evictable[frameId];
            if (was == isEvictable) return;
            evictable[frameId] = isEvictable;
            evictableCount += isEvictable ? 1 : -1;
        }
    }

    /** Drop a frame from consideration (e.g. on explicit deletion). */
    public void remove(int frameId) {
        checkRange(frameId);
        synchronized (lock) {
            if (evictable[frameId]) evictableCount--;
            evictable[frameId] = false;
            refBit[frameId] = false;
        }
    }

    public Optional<Integer> pickVictim() {
        synchronized (lock) {
            if (evictableCount == 0) return Optional.empty();

            // Worst case: every evictable frame has refBit set, so we may
            // need a full pass to clear bits and another to find a victim.
            for (int step = 0; step < 2 * numFrames; step++) {
                int idx = hand;
                hand = (hand + 1) % numFrames;

                if (!evictable[idx]) continue;
                if (refBit[idx]) {
                    refBit[idx] = false;
                    continue;
                }

                evictable[idx] = false;
                evictableCount--;
                return Optional.of(idx);
            }
            return Optional.empty(); // defensive; unreachable given invariants
        }
    }

    private void checkRange(int frameId) {
        if (frameId < 0 || frameId >= numFrames) {
            throw new IndexOutOfBoundsException(
                "frameId " + frameId + " out of [0, " + numFrames + ")");
        }
    }
}
