package com.minidb;

import com.minidb.query.operators.InsertOperator;
import com.minidb.query.operators.SeqScanOperator;
import com.minidb.recovery.WALManager;
import com.minidb.storage.*;
import com.minidb.txn.Transaction;
import com.minidb.txn.TransactionManager;
import net.sf.jsqlparser.parser.CCJSqlParserUtil;
import net.sf.jsqlparser.statement.Statement;
import net.sf.jsqlparser.statement.select.Select;
import net.sf.jsqlparser.statement.insert.Insert;
import net.sf.jsqlparser.expression.LongValue;
import net.sf.jsqlparser.expression.StringValue;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Scanner;
import java.util.logging.Level;
import java.util.logging.Logger;

public class Shell {
    private static final String DB_FILE = "minidb.db";
    private static final String WAL_FILE = "minidb.wal";
    
    private PageManager pageManager;
    private BufferPool bufferPool;
    private HeapFile heapFile;
    private WALManager walManager;
    private TransactionManager txnManager;
    private Transaction currentTxn;
    
    private List<ColumnType> defaultSchema;

    public void start() {
        // Suppress noisy logs to keep shell clean, but we will print our own meaningful ones
        Logger.getLogger("com.minidb").setLevel(Level.INFO);

        printHeader();

        try {
            defaultSchema = new ArrayList<>();
            defaultSchema.add(ColumnType.INT);
            defaultSchema.add(ColumnType.VARCHAR);

            System.out.println("[System] Initializing WAL and Transaction Manager...");
            walManager = new WALManager(WAL_FILE);
            txnManager = new TransactionManager(walManager);

            System.out.println("[System] Opening Storage Engine...");
            pageManager = new PageManager(DB_FILE);
            
            // The new BufferPool constructor automatically triggers RecoveryManager if WAL exists!
            bufferPool = new BufferPool(pageManager, 50, walManager);
            heapFile = new HeapFile(bufferPool, pageManager, defaultSchema, walManager);

            showStatus();

            Scanner scanner = new Scanner(System.in);
            while (true) {
                String prompt = (currentTxn == null) ? "minidb> " : "minidb (Txn:" + currentTxn.getId() + ")> ";
                System.out.print(prompt);
                
                if (!scanner.hasNextLine()) break;
                String input = scanner.nextLine().trim();

                if (input.equalsIgnoreCase(".exit")) break;
                if (input.equalsIgnoreCase(".status")) { showStatus(); continue; }
                if (input.equalsIgnoreCase(".help")) { showHelp(); continue; }
                
                // Transaction Commands
                if (input.equalsIgnoreCase(".begin")) { beginTxn(); continue; }
                if (input.equalsIgnoreCase(".commit")) { commitTxn(); continue; }
                
                if (input.isEmpty()) continue;
                processQuery(input);
            }

            if (currentTxn != null) {
                System.out.println("[Warning] Uncommitted transaction active. Rolling back (implicitly)...");
            }
            
            bufferPool.flushAll();
            pageManager.close();
            walManager.close();
            System.out.println("\n[System] All data and logs synced. Storage safely closed.");
            System.out.println("Goodbye!");

        } catch (Exception e) {
            System.err.println("Critical System Error: " + e.getMessage());
            e.printStackTrace();
        }
    }

    private void beginTxn() {
        if (currentTxn != null) {
            System.out.println("[Error] A transaction is already active (ID: " + currentTxn.getId() + ")");
            return;
        }
        currentTxn = txnManager.begin();
        System.out.println("[TXN] Transaction " + currentTxn.getId() + " started with snapshot: " + currentTxn.getSnapshotId());
    }

    private void commitTxn() throws IOException {
        if (currentTxn == null) {
            System.out.println("[Error] No active transaction to commit.");
            return;
        }
        long id = currentTxn.getId();
        txnManager.commit(currentTxn);
        System.out.println("[TXN] Transaction " + id + " committed successfully. WAL updated.");
        currentTxn = null;
    }

    private void processQuery(String sql) {
        try {
            if (!sql.endsWith(";")) sql += ";";
            Statement statement = CCJSqlParserUtil.parse(sql);

            if (statement instanceof Select) {
                executeSelect((Select) statement);
            } else if (statement instanceof Insert) {
                executeInsert((Insert) statement);
            } else {
                System.out.println("[Error] Command not supported.");
            }
        } catch (Exception e) {
            System.out.println("[SQL Error] " + e.getMessage());
        }
    }

    private void executeSelect(Select select) throws Exception {
        // Provide the current transaction context for MVCC visibility checks
        SeqScanOperator scan = new SeqScanOperator(heapFile, txnManager);
        scan.open();
        
        System.out.println("+------------+-------------------------+--------+--------+");
        System.out.println("| ID (INT)   | NAME (VARCHAR)          | XMIN   | XMAX   |");
        System.out.println("+------------+-------------------------+--------+--------+");
        
        Row row;
        int count = 0;
        while ((row = scan.next()) != null) {
            Object id = row.getValues()[0];
            Object name = row.getValues()[1];
            // Displaying MVCC metadata for logging/visibility
            System.out.printf("| %-10s | %-23s | %-6s | %-6s |\n", 
                id, name, row.getXmin(), row.getXmax());
            count++;
        }
        
        System.out.println("+------------+-------------------------+--------+--------+");
        System.out.println(count + " rows in set (visible to your snapshot).");
        scan.close();
    }

    private void executeInsert(Insert insert) throws Exception {
        if (insert.getValues() != null) {
            List<?> expressions = insert.getValues().getExpressions();
            Object[] data = new Object[2];
            if (expressions.size() >= 1 && expressions.get(0) instanceof LongValue) {
                data[0] = (int) ((LongValue) expressions.get(0)).getValue();
            }
            if (expressions.size() >= 2 && expressions.get(1) instanceof StringValue) {
                data[1] = ((StringValue) expressions.get(1)).getValue();
            }

            Row rowToAdd = new Row(data);
            
            // Use the new InsertOperator which integrates with TransactionManager and WAL
            InsertOperator insertOp = new InsertOperator(heapFile, Arrays.asList(rowToAdd), txnManager);
            insertOp.open();
            insertOp.next(); 
            insertOp.close();
            
            System.out.println("[System] Row inserted.");
            if (currentTxn == null) {
                System.out.println("[WAL] Warning: Insert occurred without a manual transaction. Defaulting to Auto-Commit.");
            } else {
                System.out.println("[WAL] Logical log record written for Txn: " + currentTxn.getId());
            }
        }
    }

    private void showStatus() throws IOException {
        System.out.println("\n--- Advanced Engine Status ---");
        System.out.println("Data File   : " + DB_FILE);
        System.out.println("WAL File    : " + WAL_FILE);
        System.out.println("Transactions: " + (txnManager != null ? "Active (Isolation: Snapshot)" : "Off"));
        System.out.println("Persistence : 4KB Slotted-Pages");
        System.out.println("MVCC        : Enabled (Track B - Concurrency)");
        System.out.println("Disk Pages  : " + pageManager.getNumPages());
        System.out.println("------------------------------\n");
    }

    private void showHelp() {
        System.out.println("\nAvailable Commands:");
        System.out.println("  .begin                 - Start a new transaction");
        System.out.println("  .commit                - Commit current changes to WAL");
        System.out.println("  INSERT INTO users ...  - Add data");
        System.out.println("  SELECT * FROM users;   - View data (MVCC aware)");
        System.out.println("  .status                - View engine metrics");
        System.out.println("  .exit                  - Safe shutdown");
        System.out.println();
    }

    private void printHeader() {
        System.out.println("  __  __ _       _ _____  ____  ");
        System.out.println(" |  \\/  (_)     (_)  __ \\|  _ \\ ");
        System.out.println(" | \\  / |_ _ __  _| |  | | |_) |");
        System.out.println(" | |\\/| | | '_ \\| | |  | |  _ < ");
        System.out.println(" | |  | | | | | | | |__| | |_) |");
        System.out.println(" |_|  |_|_|_| |_|_|_____/|____/ ");
        System.out.println("MiniDB Advanced Edition v2.0 | MVCC & Recovery Enabled");
        System.out.println("Created by Nandani (Capstone Project)\n");
    }

    public static void main(String[] args) {
        new Shell().start();
    }
}
