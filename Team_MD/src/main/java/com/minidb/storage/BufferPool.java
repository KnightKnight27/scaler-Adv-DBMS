package com.minidb.storage;

import com.minidb.recovery.WALManager;
import java.io.IOException;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

/**
 * An LRU cache of Pages backed by PageManager.
 */
public class BufferPool {
    private final PageManager pageManager;
    private final int capacity;
    private final WALManager walManager;
    private final Map<Integer, Page> cache;
    private final Set<Integer> dirtyPages;

    public BufferPool(PageManager pageManager, int capacity) {
        this(pageManager, capacity, null);
    }

    public BufferPool(PageManager pageManager, int capacity, WALManager walManager) {
        this.pageManager = pageManager;
        this.capacity = capacity;
        this.walManager = walManager;
        this.dirtyPages = new HashSet<>();
        
        // LinkedHashMap with access-order=true for LRU
        this.cache = new LinkedHashMap<Integer, Page>(capacity, 0.75f, true) {
            @Override
            protected boolean removeEldestEntry(Map.Entry<Integer, Page> eldest) {
                if (size() > BufferPool.this.capacity) {
                    try {
                        flushPage(eldest.getKey());
                    } catch (IOException e) {
                        throw new RuntimeException("Failed to flush page during eviction", e);
                    }
                    return true;
                }
                return false;
            }
        };

        // Auto-run recovery if a WAL manager is provided (startup scenario)
        if (walManager != null) {
            try {
                com.minidb.recovery.RecoveryManager rm =
                    new com.minidb.recovery.RecoveryManager(walManager, pageManager);
                int pagesRecovered = rm.recover();
                if (pagesRecovered > 0) {
                    java.util.logging.Logger.getLogger(BufferPool.class.getName())
                        .info("BufferPool: recovery applied to " + pagesRecovered + " page(s).");
                }
            } catch (IOException e) {
                throw new RuntimeException("Recovery failed on startup", e);
            }
        }
    }

    public Page getPage(int pageId) throws IOException {
        if (cache.containsKey(pageId)) {
            return cache.get(pageId);
        }
        Page page = pageManager.readPage(pageId);
        cache.put(pageId, page);
        return page;
    }

    public void markDirty(int pageId) {
        dirtyPages.add(pageId);
    }

    private void flushPage(int pageId) throws IOException {
        if (dirtyPages.contains(pageId)) {
            Page page = cache.get(pageId);
            if (page != null) {
                pageManager.writePage(page);
                dirtyPages.remove(pageId);
            }
        }
    }

    public void flushAll() throws IOException {
        for (int pageId : new HashSet<>(dirtyPages)) {
            flushPage(pageId);
        }
    }

    /**
     * Helper to add a new page directly to buffer pool (e.g. from PageManager.allocatePage)
     */
    public void addPage(Page page) {
        cache.put(page.getPageId(), page);
    }

    public int getLoadedPageCount() {
        return cache.size();
    }
}
