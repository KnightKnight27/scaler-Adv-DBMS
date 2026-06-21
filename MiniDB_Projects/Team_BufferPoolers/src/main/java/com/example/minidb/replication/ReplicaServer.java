package com.example.minidb.replication;

import com.example.minidb.storage.*;

import java.io.IOException;

public class ReplicaServer {

    private final TableStorage storage;

    private long lastAppliedLSN = 0;

    public ReplicaServer(
            TableStorage storage) {

        this.storage = storage;
    }

    public void receive(
        ReplicationMessage msg)
        throws IOException {

    if (msg.getLsn() <=
            lastAppliedLSN) {

        return;
    }

    lastAppliedLSN =
            msg.getLsn();

    if ("INSERT".equals(
            msg.getOperation())) {

        Row row =
                new Row(
                        java.util.List.of(
                                msg.getPayload()
                                        .split(",")
                        )
                );

        storage.insert(row);
    }
}

    public TableStorage getStorage() {
        return storage;
    }

    public java.util.List<Row> readAll()
        throws IOException {

    return storage.selectAll();
}
public void catchUp(
        java.util.List<ReplicationMessage> logs)
        throws IOException {

    for (ReplicationMessage msg : logs) {
        receive(msg);
    }
}

public long getLastAppliedLSN() {
    return lastAppliedLSN;
}

}