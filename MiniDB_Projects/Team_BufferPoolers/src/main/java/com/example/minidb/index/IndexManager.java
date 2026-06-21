package com.example.minidb.index;

public class IndexManager {

    private final BPlusTree tree;

    public IndexManager() {

        tree = new BPlusTree();
    }

    public void insert(
            int key,
            int pageId) {

        tree.insert(
                key,
                pageId
        );
    }

    public Integer lookup(
            int key) {

        return tree.search(key);
    }
}