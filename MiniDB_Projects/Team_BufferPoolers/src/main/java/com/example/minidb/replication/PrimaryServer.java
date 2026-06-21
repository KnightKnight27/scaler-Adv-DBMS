package com.example.minidb.replication;

import com.example.minidb.storage.*;
import com.example.minidb.recovery.*;

import java.io.IOException;

public class PrimaryServer {

    private final TableStorage storage;

    private final WALManager wal;

    private ReplicaServer replica;

     private final java.util.List<ReplicationMessage>
        replicationLog =
        new java.util.ArrayList<>();

    public PrimaryServer(
            TableStorage storage,
            WALManager wal) {

        this.storage = storage;
        this.wal = wal;
    }

    public void connectReplica(
            ReplicaServer replica) {

        this.replica = replica;
    }

    public void insert(Row row)
        throws IOException {

    long lsn =
        wal.log(
                "INSERT",
                row.toString()
        );

    storage.insert(row);

    ReplicationMessage msg =
        new ReplicationMessage(
                lsn,
                "INSERT",
                row.toString()
        );

    replicationLog.add(msg);

    if (replica != null) {
        replica.receive(msg);
    }
}
    public java.util.List<ReplicationMessage>
getReplicationLog() {

    return replicationLog;
}
   
}
