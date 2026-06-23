package com.minidb.recovery;

import com.minidb.storage.*;
import com.minidb.txn.Transaction;
import com.minidb.txn.TransactionManager;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Real crash-recovery test.
 *
 * Phase 1 (crash phase):  a *separate JVM process* is forked to run
 *   {@link CrashSimulator}.  That process commits txn A (Alice + Bob),
 *   starts txn B (Charlie), flushes the dirty page to disk, then calls
 *   {@code Runtime.halt(42)} — a true hard abort with no shutdown hooks,
 *   no close() calls, no BufferPool.flushAll().
 *
 * After the subprocess exits with code 42 we know:
 *   • The WAL has: INSERT(Alice) INSERT(Bob) COMMIT(A) INSERT(Charlie)
 *     — no COMMIT(B).
 *   • The .db file has all three rows physically on disk (we flushed).
 *
 * Phase 2 (recovery phase):  the test JVM opens the SAME db + wal files.
 *   Creating {@link BufferPool} with a live WALManager auto-triggers
 *   {@link RecoveryManager#recover()}, which:
 *     • REDOs A  → Alice and Bob are present.
 *     • UNDOs B  → Charlie's slot is deleted.
 *
 * Assertions:
 *   • Exactly Alice and Bob are visible (xmin = A's id, xmax = 0).
 *   • Charlie is NOT present anywhere in the page file.
 */
public class CrashRecoveryTest {

    private static final String DB_FILE  = "crash_test_table.db";
    private static final String WAL_FILE = "crash_test_wal.log";

    @BeforeEach
    public void cleanUp() {
        new File(DB_FILE).delete();
        new File(WAL_FILE).delete();
    }

    @AfterEach
    public void tearDown() {
        new File(DB_FILE).delete();
        new File(WAL_FILE).delete();
    }

    @Test
    public void testCrashAndRecovery() throws Exception {
        // ──────────────────────────────────────────────────────────────────
        // PHASE 1: run the crash-simulator in a child JVM
        // ──────────────────────────────────────────────────────────────────
        int exitCode = runCrashSimulator();
        assertEquals(42, exitCode,
            "CrashSimulator must exit via Runtime.halt(42). Got: " + exitCode);

        // Paranoia: WAL file must exist with data
        assertTrue(new File(WAL_FILE).length() > 0, "WAL file must not be empty after crash");
        assertTrue(new File(DB_FILE).length()  > 0, "DB  file must not be empty after crash");

        // ──────────────────────────────────────────────────────────────────
        // PHASE 2: restart against the same files — recovery runs automatically
        // ──────────────────────────────────────────────────────────────────
        List<ColumnType> schema = Arrays.asList(ColumnType.INT, ColumnType.VARCHAR);

        WALManager  wal = new WALManager(WAL_FILE);
        PageManager pm  = new PageManager(DB_FILE);

        // Creating BufferPool with a WALManager triggers RecoveryManager.recover()
        BufferPool bp = new BufferPool(pm, 32, wal);
        HeapFile   hf = new HeapFile(bp, pm, schema, wal);

        // Raw scan (no MVCC filter) so we see everything currently in the pages.
        List<Row> rows = hf.scan();

        // ── Assertions ────────────────────────────────────────────────────
        // 1. Exactly 2 live rows — Alice and Bob
        assertEquals(2, rows.size(),
            "After recovery: expected exactly 2 rows (Alice, Bob). Got: " + rows.size() +
            " rows: " + rowsToString(rows));

        boolean hasAlice   = rows.stream().anyMatch(r -> "Alice".equals(r.getValue(1)));
        boolean hasBob     = rows.stream().anyMatch(r -> "Bob".equals(r.getValue(1)));
        boolean hasCharlie = rows.stream().anyMatch(r -> "Charlie".equals(r.getValue(1)));

        assertTrue(hasAlice,     "Alice (committed) must be present after recovery");
        assertTrue(hasBob,       "Bob   (committed) must be present after recovery");
        assertFalse(hasCharlie,  "Charlie (uncommitted) must NOT be present after recovery");

        System.out.println("CrashRecoveryTest PASSED. Recovered rows: " + rowsToString(rows));

        pm.close();
        wal.close();
    }

    // ──────────────────────────────────────────────────────────────────────
    // Helper: fork a separate JVM running CrashSimulator
    // ──────────────────────────────────────────────────────────────────────
    private int runCrashSimulator() throws Exception {
        // Resolve absolute paths so the child process can find the files
        // relative to the test's working directory.
        String cwd    = System.getProperty("user.dir");
        String dbPath  = Paths.get(cwd, DB_FILE).toAbsolutePath().toString();
        String walPath = Paths.get(cwd, WAL_FILE).toAbsolutePath().toString();

        // Use the same JVM executable and classpath as the current process.
        String javaExec = ProcessHandle.current().info().command()
                .orElse(Paths.get(System.getProperty("java.home"), "bin", "java").toString());

        String classpath = System.getProperty("java.class.path");

        ProcessBuilder pb = new ProcessBuilder(
                javaExec,
                "-cp", classpath,
                "com.minidb.recovery.CrashSimulator",
                dbPath,
                walPath
        );
        pb.redirectErrorStream(true);                    // merge stderr → stdout
        pb.directory(new File(cwd));

        Process proc = pb.start();

        // Consume stdout so the child process doesn't block on a full pipe
        byte[] output = proc.getInputStream().readAllBytes();

        int code = proc.waitFor();
        if (code != 42) {
            System.err.println("CrashSimulator output:\n" + new String(output));
        }
        return code;
    }

    private String rowsToString(List<Row> rows) {
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < rows.size(); i++) {
            sb.append(rows.get(i).toString());
            if (i < rows.size() - 1) sb.append(", ");
        }
        sb.append("]");
        return sb.toString();
    }
}
