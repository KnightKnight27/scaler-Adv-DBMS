package com.example.minidb.storage;

import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;

public class BufferPool {

    private final int capacity;

    private final PageManager pageManager;

    private final Map<Integer, Page> cache;

    public BufferPool(
            int capacity,
            PageManager pageManager) {

        this.capacity = capacity;
        this.pageManager = pageManager;

        this.cache =
                new LinkedHashMap<>(
                        capacity,
                        0.75f,
                        true
                ) {

                    @Override
                    protected boolean removeEldestEntry(
                            Map.Entry<Integer, Page> eldest) {

                        return size()
                                > BufferPool.this.capacity;
                    }
                };
    }

    public Page getPage(int pageId)
            throws IOException {

        if (cache.containsKey(pageId)) {

            System.out.println(
                    "CACHE HIT : " + pageId
            );

            return cache.get(pageId);
        }

        System.out.println(
                "DISK READ : " + pageId
        );

        Page page =
                pageManager.readPage(pageId);

        cache.put(pageId, page);

        return page;
    }
}