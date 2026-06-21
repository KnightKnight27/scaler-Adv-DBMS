package com.example.minidb.index;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

public class BPlusTreeTest {

    @Test
    void testSplitAndSearch() {

        BPlusTree tree =
                new BPlusTree();

        tree.insert(10, 100);
        tree.insert(20, 200);
        tree.insert(30, 300);
        tree.insert(40, 400);
        tree.insert(50, 500);

        assertEquals(100,
                tree.search(10));

        assertEquals(300,
                tree.search(30));

        assertEquals(500,
                tree.search(50));
    }
}