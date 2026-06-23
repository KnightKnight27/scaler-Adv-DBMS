package minidb.storage;

import minidb.common.Types.Schema;
import java.io.*;
import java.util.*;

/**
 * BufferPool caches pages in memory to avoid hitting disk on every access.
 *
 * Responsibilities (the three things the rubric asks us to demonstrate):
 *   1. Page allocation  -> newPage()
 *   2. Page reads/writes -> fetchPage() / flushPage()
 *   3. Buffer pool usage -> bounded cache + LRU eviction + dirty-page writeback
 *
 * Eviction policy: LRU. When the pool is full and a new page is needed, we evict
 * the least-recently-used UNPINNED page, flushing it first if dirty.
 *
 * Pinning: a page in active use is "pinned" (pinCount > 0) and cannot be evicted.
 */
public final class BufferPool {
    private final DiskManager disk;
    private final Schema schema;
    private final int capacity;

    // accessOrder=true makes LinkedHashMap behave as an LRU list
    private final LinkedHashMap<Integer, Page> cache;

    // simple stats for benchmarking / viva
    public long hits = 0, misses = 0, evictions = 0;

    public BufferPool(DiskManager disk, Schema schema, int capacity) {
        this.disk = disk;
        this.schema = schema;
        this.capacity = capacity;
        this.cache = new LinkedHashMap<>(16, 0.75f, true);
    }

    /** Allocate a brand-new empty page and pin it. */
    public Page newPage() {
        int pid = disk.allocatePage();
        Page p = new Page(pid, schema);
        ensureRoom();
        cache.put(pid, p);
        p.pinCount++;
        return p;
    }

    /** Fetch a page by id, reading from disk on a cache miss. Pins the page. */
    public Page fetchPage(int pageId) {
        Page p = cache.get(pageId);
        if (p != null) {
            hits++;
            p.pinCount++;
            return p;
        }
        misses++;
        try {
            byte[] raw = disk.readPage(pageId);
            p = new Page(raw, schema);
            ensureRoom();
            cache.put(pageId, p);
            p.pinCount++;
            return p;
        } catch (IOException e) {
            throw new RuntimeException("fetchPage failed for " + pageId, e);
        }
    }

    /** Release a pin obtained via fetchPage/newPage. */
    public void unpin(Page p, boolean markDirty) {
        if (markDirty) p.dirty = true;
        if (p.pinCount > 0) p.pinCount--;
    }

    /** Evict LRU unpinned pages until there is room for one more. */
    private void ensureRoom() {
        if (cache.size() < capacity) return;
        Iterator<Map.Entry<Integer, Page>> it = cache.entrySet().iterator();
        while (it.hasNext() && cache.size() >= capacity) {
            Page victim = it.next().getValue();
            if (victim.pinCount == 0) {
                if (victim.dirty) flushPage(victim);
                it.remove();
                evictions++;
                return;
            }
        }
        // If every page is pinned we simply grow beyond capacity (rare in our workloads).
    }

    public void flushPage(Page p) {
        try {
            disk.writePage(p.getPageId(), p.data);
            p.dirty = false;
        } catch (IOException e) {
            throw new RuntimeException("flushPage failed", e);
        }
    }

    /** Flush all dirty pages — used at checkpoint/shutdown. */
    public void flushAll() {
        for (Page p : cache.values()) if (p.dirty) flushPage(p);
    }

    public int numPages() { return disk.numPages(); }
}
