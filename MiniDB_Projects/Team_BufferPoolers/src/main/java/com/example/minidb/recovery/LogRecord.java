package com.example.minidb.recovery;

public class LogRecord {

    private final long lsn;

    private final String operation;

    private final String payload;

    public LogRecord(
            long lsn,
            String operation,
            String payload) {

        this.lsn = lsn;
        this.operation = operation;
        this.payload = payload;
    }

    public long getLsn() {
        return lsn;
    }

    public String getOperation() {
        return operation;
    }

    public String getPayload() {
        return payload;
    }

    @Override
    public String toString() {

        return lsn +
                "|" +
                operation +
                "|" +
                payload;
    }
}
