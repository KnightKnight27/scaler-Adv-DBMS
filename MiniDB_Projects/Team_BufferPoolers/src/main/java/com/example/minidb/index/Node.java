package com.example.minidb.index;

import java.util.ArrayList;
import java.util.List;

public abstract class Node {

    protected List<Integer> keys;

    protected Node parent;

    public Node() {
        keys = new ArrayList<>();
    }

    public List<Integer> getKeys() {
        return keys;
    }

    public Node getParent() {
        return parent;
    }

    public void setParent(Node parent) {
        this.parent = parent;
    }

    public abstract boolean isLeaf();
}