package com.minidb.query.operators;

import com.minidb.query.Operator;
import com.minidb.storage.Row;

public class FilterOperator implements Operator {
    private Operator child;
    private Predicate predicate;

    public interface Predicate {
        boolean eval(Row row);
    }

    public FilterOperator(Operator child, Predicate predicate) {
        this.child = child;
        this.predicate = predicate;
    }

    @Override
    public void open() throws Exception {
        child.open();
    }

    @Override
    public Row next() throws Exception {
        Row row;
        while ((row = child.next()) != null) {
            if (predicate.eval(row)) {
                return row;
            }
        }
        return null;
    }

    @Override
    public void close() throws Exception {
        child.close();
    }
}
