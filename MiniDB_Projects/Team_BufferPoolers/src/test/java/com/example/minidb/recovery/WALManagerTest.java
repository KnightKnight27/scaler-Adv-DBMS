package com.example.minidb.recovery;

import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

public class WALManagerTest {

    @Test
    void testLogWrite()
            throws Exception {

        WALManager wal =
                new WALManager(
                        "database/wal.log"
                );

        wal.log(
                "INSERT",
                "1,Alice"
        );

        List<String> records =
                wal.readAll();

        assertFalse(
                records.isEmpty()
        );
    }
}
