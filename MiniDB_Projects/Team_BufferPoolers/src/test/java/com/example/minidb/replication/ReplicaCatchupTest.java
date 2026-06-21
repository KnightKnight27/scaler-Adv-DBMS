package com.example.minidb.replication;

import com.example.minidb.recovery.WALManager;
import com.example.minidb.storage.HeapFile;
import com.example.minidb.storage.PageManager;
import com.example.minidb.storage.Row;
import com.example.minidb.storage.TableStorage;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class ReplicaCatchupTest {

    @Test
    void testCatchup() throws Exception {

        PrimaryServer primary =
                new PrimaryServer(
                        new TableStorage(
                                new HeapFile(
                                        new PageManager(
                                                "database/catchup_primary.db"
                                        )
                                )
                        ),
                        new WALManager(
                                "database/catchup_primary.log"
                        )
                );

        ReplicaServer replica =
                new ReplicaServer(
                        new TableStorage(
                                new HeapFile(
                                        new PageManager(
                                                "database/catchup_replica.db"
                                        )
                                )
                        )
                );

        /*
         * IMPORTANT:
         * Do NOT connect replica.
         * We are simulating replica being offline.
         */

        primary.insert(
                new Row(
                        java.util.List.of(
                                "1",
                                "Alice"
                        )
                )
        );

        replica.catchUp(
                primary.getReplicationLog()
        );

        assertEquals(
                1,
                replica.readAll().size()
        );
    }
}