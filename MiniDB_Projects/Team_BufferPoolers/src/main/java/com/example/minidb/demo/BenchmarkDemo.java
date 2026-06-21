package com.example.minidb.demo;

import com.example.minidb.index.IndexManager;
import com.example.minidb.recovery.RecoveryManager;
import com.example.minidb.recovery.WALManager;
import com.example.minidb.replication.PrimaryServer;
import com.example.minidb.replication.ReplicaServer;
import com.example.minidb.storage.HeapFile;
import com.example.minidb.storage.PageManager;
import com.example.minidb.storage.Row;
import com.example.minidb.storage.TableStorage;

import java.io.File;
import java.util.List;

public class BenchmarkDemo {

public static void main(String[] args) throws Exception {

    System.out.println("=================================");
    System.out.println("MiniDB Benchmark Suite");
    System.out.println("=================================");

    new File("database").mkdirs();

    benchmarkStorageInsert();

    benchmarkIndexLookup();

    benchmarkRecovery();

    benchmarkReplication();

    System.out.println("=================================");
    System.out.println("Benchmark Complete");
    System.out.println("=================================");
}

private static void benchmarkStorageInsert()
        throws Exception {

    System.out.println("\n[Storage Benchmark]");

    deleteFile("database/storage_bench.db");

    PageManager pm =
            new PageManager(
                    "database/storage_bench.db"
            );

    HeapFile heap =
            new HeapFile(pm);

    TableStorage storage =
            new TableStorage(heap);

    long start = System.nanoTime();

    for (int i = 1; i <= 1000; i++) {

        storage.insert(
                new Row(
                        List.of(
                                String.valueOf(i),
                                "User" + i
                        )
                )
        );
    }

    long end = System.nanoTime();

    double ms =
            (end - start) / 1_000_000.0;

    System.out.println(
            "Inserted 1000 rows in "
                    + ms + " ms"
    );
}

private static void benchmarkIndexLookup()
        throws Exception {

    System.out.println("\n[Index Benchmark]");

    IndexManager index =
            new IndexManager();

    for (int i = 1; i <= 1000; i++) {

        index.insert(
                i,
                i
        );
    }

    long start = System.nanoTime();

    for (int i = 0; i < 1000; i++) {

        index.lookup(500);
    }

    long end = System.nanoTime();

    double ms =
            (end - start) / 1_000_000.0;

    System.out.println(
            "1000 index lookups in "
                    + ms + " ms"
    );
}

private static void benchmarkRecovery()
        throws Exception {

    System.out.println("\n[Recovery Benchmark]");

    deleteFile("database/recovery_bench.db");
    deleteFile("database/recovery_bench.log");

    WALManager wal =
            new WALManager(
                    "database/recovery_bench.log"
            );

    for (int i = 1; i <= 1000; i++) {

        wal.log(
                "INSERT",
                i + ",User" + i
        );
    }

    PageManager pm =
            new PageManager(
                    "database/recovery_bench.db"
            );

    HeapFile heap =
            new HeapFile(pm);

    TableStorage storage =
            new TableStorage(heap);

    RecoveryManager recovery =
            new RecoveryManager(
                    wal,
                    storage
            );

    long start = System.nanoTime();

    recovery.recover();

    long end = System.nanoTime();

    double ms =
            (end - start) / 1_000_000.0;

    System.out.println(
            "Recovered 1000 WAL entries in "
                    + ms + " ms"
    );

    System.out.println(
            "Recovered rows: "
                    + storage.selectAll().size()
    );
}

private static void benchmarkReplication()
        throws Exception {

    System.out.println("\n[Replication Benchmark]");

    deleteFile("database/primary.db");
    deleteFile("database/replica.db");
    deleteFile("database/primary.log");

    PageManager primaryPm =
            new PageManager(
                    "database/primary.db"
            );

    HeapFile primaryHeap =
            new HeapFile(primaryPm);

    TableStorage primaryStorage =
            new TableStorage(primaryHeap);

    WALManager wal =
            new WALManager(
                    "database/primary.log"
            );

    PrimaryServer primary =
            new PrimaryServer(
                    primaryStorage,
                    wal
            );

    PageManager replicaPm =
            new PageManager(
                    "database/replica.db"
            );

    HeapFile replicaHeap =
            new HeapFile(replicaPm);

    TableStorage replicaStorage =
            new TableStorage(replicaHeap);

    ReplicaServer replica =
            new ReplicaServer(
                    replicaStorage
            );

    primary.connectReplica(
            replica
    );

    long start = System.nanoTime();

    for (int i = 1; i <= 1000; i++) {

        primary.insert(
                new Row(
                        List.of(
                                String.valueOf(i),
                                "User" + i
                        )
                )
        );
    }

    long end = System.nanoTime();

    double ms =
            (end - start) / 1_000_000.0;

    System.out.println(
            "Replicated 1000 rows in "
                    + ms + " ms"
    );

    System.out.println(
            "Replica row count = "
                    + replica.readAll().size()
    );
}

private static void deleteFile(
        String path) {

    File file =
            new File(path);

    if (file.exists()) {
        file.delete();
    }
}


}
