package com.example.minidb.index;

import java.util.ArrayList;
import java.util.List;

public class LeafNode extends Node {

    private final List<Integer> values;

    private LeafNode next;

    public LeafNode() {
        values = new ArrayList<>();
    }

    @Override
    public boolean isLeaf() {
        return true;
    }

    public List<Integer> getValues() {
        return values;
    }

    public LeafNode getNext() {
        return next;
    }

    public void setNext(LeafNode next) {
        this.next = next;
    }
}