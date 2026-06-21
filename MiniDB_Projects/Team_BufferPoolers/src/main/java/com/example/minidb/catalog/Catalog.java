package com.example.minidb.catalog;

import java.util.HashMap;
import java.util.Map;

public class Catalog {

    private final Map<String, Table> tables =
            new HashMap<>();

    public void createTable(
            String tableName) {

        tables.put(
                tableName,
                new Table(tableName)
        );
    }

    public Table getTable(
            String tableName) {

        return tables.get(tableName);
    }
}