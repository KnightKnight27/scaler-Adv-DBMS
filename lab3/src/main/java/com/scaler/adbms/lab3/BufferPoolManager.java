package com.scaler.adbms.lab3;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/**
 * Buffer pool with clock-sweep page replacement.
 *
 * <p>Frames are slots that hold cached pages. A page table maps
 * {@code pageId -> frameId}. The free list holds frame ids that have
 * never been used; once it's empty, eviction is the only way to make room.
 *
 * <p>Public methods are synchronized through a single intrinsic lock so the
 * page table, free list, and replacer stay consistent with each other.
 */
public final class BufferPoolManager {
    private final int poolSize;
    private final DiskManager disk;
    private final List<Page> frames;
    private final Map<Integer, Integer> pageTable = new HashMap<>();
    private final Deque<Integer> freeList = new ArrayDeque<>();
    private final ClockSweepReplacer replacer;

    private long hits = 0;
    private long misses = 0;
    private long evictions = 0;

    public BufferPoolManager(int poolSize, DiskManager disk) {
        if (poolSize <= 0) {
            throw new IllegalArgumentException("poolSize must be > 0");
        }
        if (disk == null) {
            throw new IllegalArgumentException("disk must not be null");
        }
        this.poolSize = poolSize;
        this.disk = disk;
        this.frames = new ArrayList<>(poolSize);
        for (int i = 0; i < poolSize; i++) {
            frames.add(new Page());
            freeList.addLast(i);
        }
        this.replacer = new ClockSweepReplacer(poolSize);
    }

    public int poolSize() {
        return poolSize;
    }

    public synchronized BufferPoolStats stats() {
        return new BufferPoolStats(hits, misses, evictions,
            freeList.size(), pageTable.size());
    }

    /** Allocate a brand-new page on disk and pin it. */
    public synchronized Page newPage() {
        Integer frame = acquireFrame();
        if (frame == null) return null;

        int pid = disk.allocatePage();
        Page p = frames.get(frame);
        p.reset(pid);
        p.incrPin();

        pageTable.put(pid, frame);
        replacer.recordAccess(frame);
        replacer.setEvictable(frame, false);
        return p;
    }

    /** Pin and return the page, loading it from disk on miss. */
    public synchronized Page fetchPage(int pageId) {
        Integer frame = pageTable.get(pageId);
        if (frame != null) {
            Page p = frames.get(frame);
            p.incrPin();
            replacer.recordAccess(frame);
            replacer.setEvictable(frame, false);
            hits++;
            return p;
        }

        misses++;
        frame = acquireFrame();
        if (frame == null) return null;

        Page p = frames.get(frame);
        p.reset(pageId);
        disk.readPage(pageId, p.getData());
        p.incrPin();
        pageTable.put(pageId, frame);
        replacer.recordAccess(frame);
        replacer.setEvictable(frame, false);
        return p;
    }

    /** Release one pin. When pinCount hits 0 the frame becomes evictable. */
    public synchronized boolean unpinPage(int pageId, boolean isDirty) {
        Integer frame = pageTable.get(pageId);
        if (frame == null) return false;

        Page p = frames.get(frame);
        if (p.getPinCount() == 0) return false;

        p.markDirty(isDirty);
        p.decrPin();
        if (p.getPinCount() == 0) {
            replacer.setEvictable(frame, true);
        }
        return true;
    }

    public synchronized boolean flushPage(int pageId) {
        Integer frame = pageTable.get(pageId);
        if (frame == null) return false;
        Page p = frames.get(frame);
        disk.writePage(pageId, p.getData());
        p.clearDirty();
        return true;
    }

    public synchronized void flushAll() {
        for (Integer pid : new ArrayList<>(pageTable.keySet())) {
            flushPage(pid);
        }
    }

    /** Remove a page from the pool and deallocate it on disk.
     *  Returns false when the page is still pinned. */
    public synchronized boolean deletePage(int pageId) {
        Integer frame = pageTable.get(pageId);
        if (frame == null) {
            disk.deallocatePage(pageId);
            return true;
        }

        Page p = frames.get(frame);
        if (p.getPinCount() > 0) return false;

        replacer.remove(frame);
        pageTable.remove(pageId);
        p.reset(Page.INVALID_PAGE_ID);
        freeList.addLast(frame);
        disk.deallocatePage(pageId);
        return true;
    }

    /** Caller must already hold the monitor. Returns a frame ready to hold
     *  a new page. Evicts via clock sweep when the free list is empty.
     *  Writes the victim back to disk first if it was dirty. */
    private Integer acquireFrame() {
        if (!freeList.isEmpty()) {
            return freeList.pollFirst();
        }

        Optional<Integer> victim = replacer.pickVictim();
        if (victim.isEmpty()) return null;

        int frame = victim.get();
        Page victimPage = frames.get(frame);
        if (victimPage.isDirty()) {
            disk.writePage(victimPage.getPageId(), victimPage.getData());
        }
        pageTable.remove(victimPage.getPageId());
        victimPage.reset(Page.INVALID_PAGE_ID);
        evictions++;
        return frame;
    }
}
