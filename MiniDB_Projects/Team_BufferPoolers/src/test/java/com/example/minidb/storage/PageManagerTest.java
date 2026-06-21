package com.example.minidb.storage;


import org.junit.jupiter.api.Test;


import static org.junit.jupiter.api.Assertions.assertEquals;

public class PageManagerTest {

    @Test
    public void testReadWrite() throws Exception {

        PageManager pm =
            new PageManager(
                "database/test.db"
            );

        int pageId =
            pm.allocatePage();

        Page page =
            new Page(pageId);

        page.setData(
            "hello".getBytes()
        );

        pm.writePage(page);

        Page loaded =
            pm.readPage(pageId);

        String result =
            new String(
                loaded.getData()
            ).trim();

        assertEquals(
            "hello",
            result
        );
    }
}
