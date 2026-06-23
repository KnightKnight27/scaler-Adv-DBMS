package com.minidb.query;

import com.minidb.storage.Row;

public interface Operator {
    void open() throws Exception;
    Row next() throws Exception;
    void close() throws Exception;
}
