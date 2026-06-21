package com.example.minidb.replication;
public class ReplicationMessage {

    private final long lsn;

    private final String operation;

    private final String payload;

    public ReplicationMessage(
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
}
