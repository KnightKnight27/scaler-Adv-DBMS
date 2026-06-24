package com.scaler.adbms.lab3;

/** Snapshot of pool counters at one point in time. */
public record BufferPoolStats(
    long hits,
    long misses,
    long evictions,
    int freeFrames,
    int cachedPages
) {
    public double hitRate() {
        long total = hits + misses;
        return total == 0 ? 0.0 : (double) hits / total;
    }
}
