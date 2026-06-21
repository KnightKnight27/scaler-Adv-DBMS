package com.example.minidb.index;

import java.util.ArrayList;
import java.util.List;

public class InternalNode extends Node {

    private final List<Node> children;

    public InternalNode() {
        children = new ArrayList<>();
    }

    @Override
    public boolean isLeaf() {
        return false;
    }

    public List<Node> getChildren() {
        return children;
    }
}