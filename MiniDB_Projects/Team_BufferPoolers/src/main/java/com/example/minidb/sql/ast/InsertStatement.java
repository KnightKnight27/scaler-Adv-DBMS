package com.example.minidb.sql.ast;

import java.util.List;

public class InsertStatement
        implements Statement {

    private final String tableName;

    private final List<String> values;

    public InsertStatement(
            String tableName,
            List<String> values) {

        this.tableName = tableName;
        this.values = values;
    }

    public String getTableName() {
        return tableName;
    }

    public List<String> getValues() {
        return values;
    }
}