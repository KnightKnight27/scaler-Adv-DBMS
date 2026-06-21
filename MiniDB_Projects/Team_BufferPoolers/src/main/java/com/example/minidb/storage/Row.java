package com.example.minidb.storage;

import java.util.List;

public class Row {

    private final List<String> values;

    public Row(List<String> values) {
        this.values = values;
    }

    public List<String> getValues() {
        return values;
    }

    @Override
    public String toString() {
        return String.join(",", values);
    }
}
