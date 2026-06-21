package com.example.minidb.index;

public class BPlusTree {

    private static final int ORDER = 4;

    private Node root;

    public BPlusTree() {
        root = new LeafNode();
    }

    public Integer search(int key) {

        LeafNode leaf = findLeaf(key);

        for (int i = 0; i < leaf.getKeys().size(); i++) {

            if (leaf.getKeys().get(i) == key) {
                return leaf.getValues().get(i);
            }
        }

        return null;
    }

    public void insert(int key, int value) {

        LeafNode leaf = findLeaf(key);

        insertIntoLeaf(
                leaf,
                key,
                value
        );

        if (leaf.getKeys().size() >= ORDER) {
            splitLeaf(leaf);
        }
    }

    private LeafNode findLeaf(int key) {

        Node current = root;

        while (!current.isLeaf()) {

            InternalNode internal =
                    (InternalNode) current;

            int i = 0;

            while (i < internal.getKeys().size()
                    && key >= internal.getKeys().get(i)) {
                i++;
            }

            current =
                    internal.getChildren().get(i);
        }

        return (LeafNode) current;
    }

    private void insertIntoLeaf(
            LeafNode leaf,
            int key,
            int value) {

        int pos = 0;

        while (pos < leaf.getKeys().size()
                && leaf.getKeys().get(pos) < key) {
            pos++;
        }

        leaf.getKeys().add(pos, key);
        leaf.getValues().add(pos, value);
    }

    private void splitLeaf(
            LeafNode leaf) {

        int mid =
                leaf.getKeys().size() / 2;

        LeafNode newLeaf =
                new LeafNode();

        while (leaf.getKeys().size() > mid) {

            newLeaf.getKeys().add(
                    leaf.getKeys().remove(mid)
            );

            newLeaf.getValues().add(
                    leaf.getValues().remove(mid)
            );
        }

        newLeaf.setNext(
                leaf.getNext()
        );

        leaf.setNext(
                newLeaf
        );

        int promotedKey =
                newLeaf.getKeys().get(0);

        insertIntoParent(
                leaf,
                promotedKey,
                newLeaf
        );
    }

    private void insertIntoParent(
            Node left,
            int key,
            Node right) {

        if (left == root) {

            InternalNode newRoot =
                    new InternalNode();

            newRoot.getKeys().add(key);

            newRoot.getChildren().add(left);
            newRoot.getChildren().add(right);

            left.setParent(newRoot);
            right.setParent(newRoot);

            root = newRoot;

            return;
        }

        InternalNode parent =
                (InternalNode) left.getParent();

        int pos = 0;

        while (pos < parent.getKeys().size()
                && parent.getKeys().get(pos) < key) {
            pos++;
        }

        parent.getKeys().add(pos, key);

        parent.getChildren().add(
                pos + 1,
                right
        );

        right.setParent(parent);
    }
}