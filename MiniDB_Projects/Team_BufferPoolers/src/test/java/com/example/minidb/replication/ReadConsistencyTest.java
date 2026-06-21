package com.example.minidb.replication;

import com.example.minidb.storage.*;
import com.example.minidb.recovery.*;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

public class ReadConsistencyTest {

    @Test
    void testReplicaReads()
            throws Exception {

        PrimaryServer primary =
                new PrimaryServer(
                        new TableStorage(
                                new HeapFile(
                                        new PageManager(
                                                "database/primary_read.db"
                                        )
                                )
                        ),
                        new WALManager(
                                "database/primary_read.log"
                        )
                );

        ReplicaServer replica =
                new ReplicaServer(
                        new TableStorage(
                                new HeapFile(
                                        new PageManager(
                                                "database/replica_read.db"
                                        )
                                )
                        )
                );

        primary.connectReplica(replica);

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
                replica.readAll().size()
        );
    }
}