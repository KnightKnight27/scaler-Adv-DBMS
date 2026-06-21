package com.example.minidb.recovery;

import java.io.*;
import java.util.*;

public class WALManager {

    private final String walFile;

    private long nextLSN = 1;

    public WALManager(
            String walFile) {

        this.walFile = walFile;
    }

    public synchronized long log(
            String operation,
            String payload)
            throws IOException {

        long lsn = nextLSN++;

        LogRecord record =
                new LogRecord(
                        lsn,
                        operation,
                        payload
                );

        try (FileWriter writer =
                     new FileWriter(
                             walFile,
                             true
                     )) {

            writer.write(
                    record.toString()
            );

            writer.write("\n");
        }

        return lsn;
    }

    public List<String> readAll()
            throws IOException {

        File file =
                new File(walFile);

        if (!file.exists()) {
            return List.of();
        }

        List<String> records =
                new ArrayList<>();

        try (BufferedReader reader =
                     new BufferedReader(
                             new FileReader(file)
                     )) {

            String line;

            while ((line = reader.readLine()) != null) {
                records.add(line);
            }
        }

        return records;
    }
}
