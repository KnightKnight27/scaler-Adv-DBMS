package com.example.minidb.optimizer;

public class QueryPlan {

    public enum Type {
        TABLE_SCAN,
        INDEX_SCAN
    }

    private final Type type;

    public QueryPlan(Type type) {
        this.type = type;
    }

    public Type getType() {
        return type;
    }
}