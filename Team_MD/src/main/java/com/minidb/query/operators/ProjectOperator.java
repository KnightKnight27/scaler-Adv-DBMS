package com.minidb.query.operators;

import com.minidb.query.Operator;
import com.minidb.storage.Row;

import java.util.ArrayList;
import java.util.List;

public class ProjectOperator implements Operator {
    private Operator child;
    private List<Integer> columnIndices;

    public ProjectOperator(Operator child, List<Integer> columnIndices) {
        this.child = child;
        this.columnIndices = columnIndices;
    }

    @Override
    public void open() throws Exception {
        child.open();
    }

    @Override
    public Row next() throws Exception {
        Row row = child.next();
        if (row == null) return null;

        List<Object> projectedValues = new ArrayList<>();
        for (int index : columnIndices) {
            projectedValues.add(row.getValue(index));
        }
        return new Row(projectedValues);
    }

    @Override
    public void close() throws Exception {
        child.close();
    }
}
