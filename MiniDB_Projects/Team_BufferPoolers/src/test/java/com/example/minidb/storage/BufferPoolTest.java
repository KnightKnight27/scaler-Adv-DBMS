package com.example.minidb.storage;

import org.junit.jupiter.api.Test;


public class BufferPoolTest {

    @Test
    public void testBufferPool()
            throws Exception {

        PageManager pm =
                new PageManager(
                        "database/test.db"
                );

        int pageId =
                pm.allocatePage();

        Page page =
                new Page(pageId);

        page.setData(
                "Alice".getBytes()
        );

        pm.writePage(page);

        BufferPool pool =
                new BufferPool(
                        2,
                        pm
                );

        pool.getPage(pageId);

        pool.getPage(pageId);

        pool.getPage(pageId);
    }
}
