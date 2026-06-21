package com.example.minidb.recovery;

import com.example.minidb.storage.*;

import java.io.IOException;

public class RecoveryManager {

    private final WALManager walManager;

    private final TableStorage storage;

    public RecoveryManager(
            WALManager walManager,
            TableStorage storage) {

        this.walManager = walManager;
        this.storage = storage;
    }

    public void recover()
            throws IOException {

        for (String record :
                walManager.readAll()) {

            String[] parts =
                    record.split("\\|");

            String operation =
                    parts[1];

            String payload =
                    parts[2];

            if ("INSERT".equals(operation)) {

                Row row =
                        new Row(
                                java.util.List.of(
                                        payload.split(",")
                                )
                        );

                storage.insert(row);
            }
        }
    }
}