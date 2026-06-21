package com.example.minidb.replication;

import com.example.minidb.storage.*;
import com.example.minidb.recovery.*;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

public class ReplicationTest {

    @Test
    void testReplication()
            throws Exception {

        PageManager pm1 =
                new PageManager(
                        "database/primary.db"
                );

        PageManager pm2 =
                new PageManager(
                        "database/replica.db"
                );

        PrimaryServer primary =
                new PrimaryServer(
                        new TableStorage(
                                new HeapFile(pm1)
                        ),
                        new WALManager(
                                "database/primary.log"
                        )
                );

        ReplicaServer replica =
                new ReplicaServer(
                        new TableStorage(
                                new HeapFile(pm2)
                        )
                );

        primary.connectReplica(
                replica
        );

        primary.insert(
                new Row(
                        java.util.List.of(
                                "1",
                                "Alice"
                        )
                )
        );

        assertEquals(
                1,
                replica.getStorage()
                        .selectAll()
                        .size()
        );
        assertEquals(
        primary.getReplicationLog().size(),
        replica.getLastAppliedLSN()
);
    }
}