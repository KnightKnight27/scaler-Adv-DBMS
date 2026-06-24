#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <memory>
#include <algorithm>
#include <filesystem>
#include <cstdint>
#include "wal.h"
#include "replication.h"
#include "execution.h"
#include "parser.h"
#include "optimizer.h"
#include "transaction.h"

namespace fs = std::filesystem;
using namespace minidb;

// Globals for DB state in main.cpp
Catalog catalog;
Table* students_table = nullptr;
std::unique_ptr<LogManager> wal_manager;
std::unique_ptr<ReplicationNode> rep_node;
bool is_primary = true;

LockManager lock_mgr;          // Strict 2PL (Phase D)
TransactionManager txn_mgr;    // per-statement auto-commit transactions

std::mutex global_db_lock;
std::mutex console_mutex;

// Trims whitespace
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Executes one SQL statement against the catalog through the full pipeline:
//   raw text -> Parser (AST) -> Optimizer (cost-based plan) -> operator tree.
// Each statement runs as its own transaction (auto-commit): it acquires table
// locks under Strict 2PL via the shared LockManager and records undo actions,
// so a failure rolls back. `EXPLAIN <query>` prints the plan instead of running
// it. Returns true on success. Never throws.
bool execute_sql_statement(const std::string& sql) {
    Statement stmt;
    try {
        stmt = Parser::parse(sql);
    } catch (const std::exception& e) {
        std::cout << e.what() << "\n";
        return false;
    }

    Optimizer optimizer(catalog);

    if (stmt.explain) {
        try {
            ExecContext ctx;  // plan only; no execution, no locks
            PhysicalPlan plan = optimizer.optimize(stmt, ctx);
            std::cout << "\nQuery plan (estimated cost " << plan.cost << "):\n"
                      << plan.explain << "\n";
            return true;
        } catch (const std::exception& e) {
            std::cout << "[ERROR] " << e.what() << "\n";
            return false;
        }
    }

    Transaction* txn = txn_mgr.begin();
    ExecContext ctx{&lock_mgr, txn->id(), txn};
    try {
        PhysicalPlan plan = optimizer.optimize(stmt, ctx);

        if (plan.is_dml) {
            execute(*plan.root);
            if (auto* ins = dynamic_cast<Insert*>(plan.root.get()))
                std::cout << "Executed locally: " << ins->inserted() << " row(s) inserted.\n";
            else if (auto* del = dynamic_cast<Delete*>(plan.root.get()))
                std::cout << "Executed locally: " << del->deleted() << " row(s) deleted.\n";
        } else {
            const Schema& out = plan.root->schema();
            auto rows = execute(*plan.root);
            std::cout << "\n";
            for (size_t c = 0; c < out.size(); ++c)
                std::cout << (c ? "\t" : "") << out[c].name;
            std::cout << "\n-----------------\n";
            for (const auto& row : rows) {
                for (size_t c = 0; c < row.size(); ++c)
                    std::cout << (c ? "\t" : "") << row[c].toString();
                std::cout << "\n";
            }
            std::cout << "(" << rows.size() << " rows)\n\n";
        }

        txn_mgr.commit(txn);            // success: keep changes
        lock_mgr.release_all(txn->id());
        return true;
    } catch (const std::exception& e) {
        txn_mgr.abort(txn);             // failure: roll back via undo log
        lock_mgr.release_all(txn->id());
        std::cout << "[ERROR] " << e.what() << "\n";
        return false;
    }
}

// Safely prints from the background thread without breaking the CLI prompt
void printFromReplica(const std::string& msg) {
    std::lock_guard<std::mutex> lock(console_mutex);
    std::cout << "\n" << msg << "\nminidb-replica> " << std::flush;
}

// Replica node callback wrapper
void replica_execution_callback(const std::string& sql) {
    printFromReplica("[REPLICA] Received replication stream event: " + sql);
    std::lock_guard<std::mutex> db_lock(global_db_lock);
    execute_sql_statement(sql);
}

// Background daemon: every 5 minutes, flush all table pages to disk (real
// checkpoint) and truncate the WAL up to that point, bounding recovery work.
void auto_checkpoint_loop() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::minutes(5));
        {
            std::lock_guard<std::mutex> console_lock(console_mutex);
            std::cout << "\n[SYSTEM] Auto-Checkpoint triggered. Freezing transactions...\n" << std::flush;
        }
        std::lock_guard<std::mutex> db_lock(global_db_lock);
        catalog.checkpointAll();                  // flush dirty pages to .dat/.idx
        std::ofstream("wal.log", std::ios::trunc); // truncate WAL up to checkpoint
        {
            std::lock_guard<std::mutex> console_lock(console_mutex);
            std::cout << "[SYSTEM] Checkpoint complete. WAL truncated. Transactions unpaused.\nminidb-primary> " << std::flush;
        }
    }
}

// A table is "fresh" (truncate + create) only if its data file is absent/empty;
// otherwise reopen the durable state so it survives restarts (recovery).
static bool freshFor(const std::string& name) {
    std::error_code ec;
    std::string p = name + ".dat";
    return !fs::exists(p, ec) || fs::file_size(p, ec) == 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <mode: primary|replica> [replica_ip] [replica_port]\n";
        return 1;
    }

    std::string mode = argv[1];
    std::string ip = "127.0.0.1";
    int port = 9999;

    if (argc >= 3) ip = argv[2];
    if (argc >= 4) port = std::stoi(argv[3]);

    // Initialize database schema. Tables are page-backed and durable: pass
    // fresh=false when their files already exist so data survives a restart.
    Schema schema = { {"id", ValueType::Int}, {"name", ValueType::Text} };
    students_table = catalog.createTable("students", schema, 0, freshFor("students"));

    // A second table so JOINs are demonstrable end-to-end through SQL, e.g.:
    //   SELECT * FROM students JOIN enroll ON students.id = enroll.sid
    Schema enroll_schema = { {"sid", ValueType::Int}, {"course", ValueType::Text} };
    catalog.createTable("enroll", enroll_schema, -1, freshFor("enroll"));

    if (mode == "primary") {
        is_primary = true;

        // 1. Recovery: durable pages are already loaded above; now replay the
        //    committed transactions recorded in the WAL after the last
        //    checkpoint, then re-checkpoint and truncate the log.
        if (fs::exists("wal.log")) {
            std::cout << "[RECOVERY] Replaying committed transactions from wal.log...\n";
            auto stmts = committedStatements("wal.log");
            int applied = 0;
            {
                std::lock_guard<std::mutex> db_lock(global_db_lock);
                for (const auto& s : stmts) {
                    if (execute_sql_statement(s)) ++applied;
                }
                catalog.checkpointAll();                   // make durable
                std::ofstream("wal.log", std::ios::trunc); // start a fresh log
            }
            std::cout << "[RECOVERY] Complete. Replayed " << applied
                      << " committed statement(s).\n\n";
        }

        // 2. Spawn the automated checkpoint thread (flushes table pages).
        std::thread checkpoint_daemon(auto_checkpoint_loop);
        checkpoint_daemon.detach();

        // 3. Initialize log manager and replication node.
        wal_manager = std::make_unique<LogManager>("wal.log");
        rep_node = std::make_unique<ReplicationNode>(ip, port);

        std::cout << "Database started in PRIMARY mode. Replicating to " << ip << ":" << port << "\n";
        std::cout << "SQL supported: INSERT INTO <t> VALUES (...) | DELETE FROM <t> [WHERE <bool-expr>]\n"
                  << "               SELECT <cols|*> FROM <t> [JOIN <t2> ON a=b] [WHERE <bool-expr>]\n"
                  << "               (<bool-expr> supports AND/OR and parentheses)\n"
                  << "               EXPLAIN <query>   (show the optimizer's plan) | exit\n\n";

        uint64_t wal_txn = 0;
        std::string line;
        while (true) {
            std::cout << "minidb-primary> ";
            if (!std::getline(std::cin, line)) break;
            line = trim(line);
            if (line == "exit" || line == "quit") break;
            if (line.empty()) continue;

            std::string upper = line;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            bool is_write = (upper.rfind("INSERT", 0) == 0 || upper.rfind("DELETE", 0) == 0);

            if (is_write) {
                // Write-ahead: log BEGIN + statement (durable) BEFORE applying.
                uint64_t t = ++wal_txn;
                wal_manager->logBegin(t);
                wal_manager->logStatement(t, line);
                std::cout << "[WAL] BEGIN + STMT logged (txn " << t << ").\n";

                // Synchronously replicate; on failure abort (no COMMIT logged).
                std::cout << "[REPLICATION] Replicating statement to backup...\n";
                if (!rep_node->sendLogToReplica(line)) {
                    std::cerr << "[ERROR] Replication failed; aborting (no COMMIT logged).\n";
                    continue;
                }
                std::cout << "[REPLICATION] Replica ACKed OK.\n";

                bool ok;
                { std::lock_guard<std::mutex> db_lock(global_db_lock); ok = execute_sql_statement(line); }
                if (ok) {
                    wal_manager->logCommit(t);            // commit point
                    std::cout << "[WAL] COMMIT logged (txn " << t << ").\n";
                }
            } else {
                std::lock_guard<std::mutex> db_lock(global_db_lock);
                execute_sql_statement(line);
            }
        }

        // Clean shutdown: flush pages to disk, then truncate the WAL so a fresh
        // start sees the durable data and an empty log (no double replay).
        std::lock_guard<std::mutex> db_lock(global_db_lock);
        catalog.checkpointAll();
        std::ofstream("wal.log", std::ios::trunc);
    }
    else if (mode == "replica") {
        is_primary = false;
        rep_node = std::make_unique<ReplicationNode>(ip, port);
        std::cout << "Database started in REPLICA mode. Listening on port " << port << "...\n";

        std::thread background_listener([]() {
            try {
                rep_node->startReplicaServer(replica_execution_callback);
            } catch (const std::exception&) {
                // Clean exit when stopReplicaServer is called
            }
        });

        std::string input_sql;
        while (true) {
            {
                std::lock_guard<std::mutex> lock(console_mutex);
                std::cout << "minidb-replica> " << std::flush;
            }
            if (!std::getline(std::cin, input_sql)) break;
            input_sql = trim(input_sql);
            if (input_sql == "exit" || input_sql == "quit") break;
            if (input_sql.empty()) continue;

            std::string upper_query = input_sql;
            std::transform(upper_query.begin(), upper_query.end(), upper_query.begin(), ::toupper);

            // Read-only replica: bounce writes.
            if (upper_query.rfind("INSERT", 0) == 0 || upper_query.rfind("DELETE", 0) == 0) {
                std::lock_guard<std::mutex> lock(console_mutex);
                std::cout << "[ERROR] Cannot execute write query on a read-only replica.\n";
                continue;
            }

            {
                std::lock_guard<std::mutex> db_lock(global_db_lock);
                execute_sql_statement(input_sql);
            }
        }

        rep_node->stopReplicaServer();
        if (background_listener.joinable()) {
            background_listener.join();
        }
    }
    else {
        std::cerr << "Invalid mode: " << mode << ". Use 'primary' or 'replica'.\n";
        return 1;
    }

    return 0;
}
