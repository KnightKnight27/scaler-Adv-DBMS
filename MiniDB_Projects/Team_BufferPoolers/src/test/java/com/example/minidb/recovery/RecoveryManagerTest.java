
package com.example.minidb.recovery;

import com.example.minidb.storage.HeapFile;
import com.example.minidb.storage.PageManager;
import com.example.minidb.storage.Row;
import com.example.minidb.storage.TableStorage;

import org.junit.jupiter.api.Test;

import java.io.File;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class RecoveryManagerTest {

    @Test
    void testRecovery() throws Exception {

        // Clean previous test files
        new File(
                "database/test_recovery.log"
        ).delete();

        new File(
                "database/test_recovery.db"
        ).delete();

        WALManager wal =
                new WALManager(
                        "database/test_recovery.log"
                );

        wal.log(
                "INSERT",
                "1,Alice"
        );

        PageManager pm =
                new PageManager(
                        "database/test_recovery.db"
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

        List<Row> rows =
                storage.selectAll();

        assertEquals(
                1,
                rows.size()
        );

        assertEquals(
                "1",
                rows.get(0)
                    .getValues()
                    .get(0)
        );

        assertEquals(
                "Alice",
                rows.get(0)
                    .getValues()
                    .get(1)
        );
    }
}