package com.example.minidb.storage;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

public class HeapFileTest {

    @Test
    void testInsertAndRead()
            throws Exception {

        PageManager pm =
                new PageManager(
                        "database/heap.db"
                );

        HeapFile heap =
                new HeapFile(pm);

        int pageId =
                heap.insertRecord(
                        "Alice"
                );

        List<String> rows =
                heap.readAllRecords(
                        pageId
                );

        assertEquals(
                1,
                rows.size()
        );

        assertEquals(
                "Alice",
                rows.get(0)
        );
    }
}