package minidb;

import minidb.exec.Executor;
import minidb.recovery.*;
import minidb.sql.Catalog;
import minidb.txn.*;
import java.io.*;

/**
 * MiniDB — the top-level engine. Wires together every subsystem:
 *
 *   Catalog (+ per-table DiskManager/BufferPool/heap files) -> storage
 *   B+ tree indexes (in Catalog)                            -> indexing
 *   WAL                                                     -> durability log
 *   TransactionManager + LockManager                        -> 2PL isolation
 *   RecoveryManager                                         -> crash recovery
 *   Optimizer + Executor                                    -> SQL planning/exec
 *
 * Usage:
 *   java -cp out minidb.MiniDB                 # interactive REPL
 *   java -cp out minidb.MiniDB script.sql      # run a SQL script
 *   java -cp out minidb.MiniDB --plan          # show EXPLAIN plans
 *
 * REPL dot-commands: .plan on|off  .stats  .recover  .tables  .exit
 */
public final class MiniDB {
    private final WAL wal;
    private final Catalog catalog;
    private final TransactionManager txnManager;
    private final Executor executor;
    private final String dataDir;

    public MiniDB(String dataDir) throws IOException {
        this.dataDir = dataDir;
        new File(dataDir).mkdirs();
        this.wal = new WAL(dataDir + "/minidb.wal");
        this.catalog = new Catalog(wal, dataDir, 128);
        this.txnManager = new TransactionManager(wal, new LockManager());
        this.executor = new Executor(catalog, txnManager);
        catalog.load();
    }

    public String exec(String sql) { return executor.execute(sql); }
    public Catalog catalog() { return catalog; }
    public WAL wal() { return wal; }
    public Executor executor() { return executor; }
    public TransactionManager txnManager() { return txnManager; }

    public void repl() throws IOException {
        BufferedReader in = new BufferedReader(new InputStreamReader(System.in));
        System.out.println("MiniDB ready. End statements with ';'. Type .help for commands.");
        String line;
        StringBuilder buf = new StringBuilder();
        System.out.print("minidb> ");
        while ((line = in.readLine()) != null) {
            String trimmed = line.trim();
            if (trimmed.startsWith(".")) {
                if (handleDot(trimmed)) break;
                System.out.print("minidb> ");
                continue;
            }
            buf.append(line).append('\n');
            if (trimmed.endsWith(";")) {
                runOne(buf.toString());
                buf.setLength(0);
                System.out.print("minidb> ");
            }
        }
        shutdown();
    }

    private void runOne(String sql) {
        sql = sql.replaceAll(";\\s*$", "").trim();
        if (sql.isEmpty()) return;
        try { System.out.println(exec(sql)); }
        catch (Exception e) { System.out.println("ERROR: " + e.getMessage()); }
    }

    private boolean handleDot(String cmd) {
        String[] p = cmd.split("\\s+");
        switch (p[0]) {
            case ".exit": case ".quit": shutdown(); return true;
            case ".help":
                System.out.println(".plan on|off, .stats, .recover, .tables, .exit");
                break;
            case ".plan":
                executor.showPlan = p.length > 1 && p[1].equalsIgnoreCase("on");
                System.out.println("Plan display: " + executor.showPlan);
                break;
            case ".stats":
                long h=0,m=0,e=0,pg=0;
                for (var t : catalog.allTables()) {
                    h+=t.pool().hits; m+=t.pool().misses; e+=t.pool().evictions; pg+=t.pool().numPages();
                }
                System.out.printf("Buffer pools: hits=%d misses=%d evictions=%d totalPages=%d%n", h,m,e,pg);
                break;
            case ".recover":
                System.out.println(new RecoveryManager(wal, catalog).recover());
                break;
            case ".tables":
                catalog.allTables().forEach(t -> System.out.println("  " + t.name));
                break;
            default: System.out.println("Unknown command: " + p[0]);
        }
        return false;
    }

    public void shutdown() {
        try { catalog.flushAll(); catalog.persist(); wal.close(); }
        catch (IOException ignored) {}
    }

    public static void main(String[] args) throws IOException {
        String dataDir = "data";
        boolean plan = false;
        String script = null;
        for (String a : args) {
            if (a.equals("--plan")) plan = true;
            else if (a.startsWith("--dir=")) dataDir = a.substring(6);
            else script = a;
        }
        MiniDB db = new MiniDB(dataDir);
        db.executor.showPlan = plan;
        if (script != null) {
            String content = new String(java.nio.file.Files.readAllBytes(new File(script).toPath()));
            for (String stmt : content.split(";")) {
                if (stmt.trim().isEmpty() || stmt.trim().startsWith("--")) continue;
                System.out.println("minidb> " + stmt.trim() + ";");
                try { System.out.println(db.exec(stmt.trim())); }
                catch (Exception e) { System.out.println("ERROR: " + e.getMessage()); }
            }
            db.shutdown();
        } else {
            db.repl();
        }
    }
}
