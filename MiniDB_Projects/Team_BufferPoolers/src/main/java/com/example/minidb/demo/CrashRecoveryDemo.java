package com.example.minidb.demo;

import com.example.minidb.recovery.*;
import com.example.minidb.storage.*;

public class CrashRecoveryDemo {

    public static void main(String[] args)
            throws Exception {

        WALManager wal =
                new WALManager(
                        "database/demo.log"
                );

        wal.log(
                "INSERT",
                "1,Alice"
        );

        System.out.println(
                "System crashed..."
        );

        PageManager pm =
                new PageManager(
                        "database/demo.db"
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

        recovery.recover();

        System.out.println(
                storage.selectAll()
        );
    }
}
