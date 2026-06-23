#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <memory>
#include <algorithm>
#include "wal.h"
#include "replication.h"
#include "execution.h"
#include "buffer_pool.h"
#include "heap_file.h"

using namespace minidb;

// Globals for DB state in main.cpp
Catalog catalog;
Table* students_table = nullptr;
std::unique_ptr<LogManager> wal_manager;
std::unique_ptr<ReplicationNode> rep_node;
bool is_primary = true;

std::mutex global_db_lock;
std::mutex console_mutex;

// Helper to split string
std::vector<std::string> split(const std::string& str, char delim) {
    std::vector<std::string> tokens;
    std::string token;
    std::stringstream ss(str);
    while (std::getline(ss, token, delim)) {
        tokens.push_back(token);
    }
    return tokens;
}

// Trims whitespace
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Executes a SQL statement on the local database table
void execute_sql_statement(const std::string& sql) {
    std::string query = trim(sql);
    std::string upper_query = query;
    std::transform(upper_query.begin(), upper_query.end(), upper_query.begin(), ::toupper);

    if (upper_query.rfind("INSERT", 0) == 0) {
        // Simple parser: INSERT INTO students VALUES (<id>, '<name>')
        size_t open_paren = query.find('(');
        size_t close_paren = query.find(')');
        if (open_paren == std::string::npos || close_paren == std::string::npos) {
            std::cout << "Invalid INSERT statement format.\n";
            return;
        }
        std::string vals_str = query.substr(open_paren + 1, close_paren - open_paren - 1);
        auto parts = split(vals_str, ',');
        if (parts.size() != 2) {
            std::cout << "Arity mismatch for table 'students'. Expected 2 values.\n";
            return;
        }
        int64_t id = std::stoll(trim(parts[0]));
        std::string name = trim(parts[1]);
        if (name.front() == '\'' && name.back() == '\'') {
            name = name.substr(1, name.length() - 2);
        }

        Tuple row = { Value::Int(id), Value::Text(name) };
        ExecContext ctx;
        Insert insert_op(students_table, row, ctx);
        execute(insert_op);
        std::cout << "Executed locally: 1 row inserted (id: " << id << ", name: " << name << ")\n";
    }
    else if (upper_query.rfind("DELETE", 0) == 0) {
        // Simple parser: DELETE FROM students WHERE id = <id>
        size_t eq_idx = query.find('=');
        if (eq_idx == std::string::npos) {
            std::cout << "Invalid DELETE statement format.\n";
            return;
        }
        int64_t id = std::stoll(trim(query.substr(eq_idx + 1)));
        
        ExecContext ctx;
        auto scan = std::make_unique<TableScan>(students_table, ctx);
        Predicate pred{ 0, CompareOp::Eq, Value::Int(id) };
        auto filter = std::make_unique<Filter>(std::move(scan), pred);
        Delete delete_op(students_table, std::move(filter), ctx);
        execute(delete_op);
        std::cout << "Executed locally: deleted rows with id = " << id << "\n";
    }
    else if (upper_query.rfind("SELECT", 0) == 0) {
        // SELECT * FROM students
        ExecContext ctx;
        TableScan scan(students_table, ctx);
        auto rows = execute(scan);
        std::cout << "\nid\tname\n";
        std::cout << "-----------------\n";
        for (const auto& row : rows) {
            std::cout << row[0].toString() << "\t" << row[1].toString() << "\n";
        }
        std::cout << "(" << rows.size() << " rows)\n\n";
    }
    else {
        std::cout << "Unknown or unsupported command.\n";
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
    try {
        execute_sql_statement(sql);
    } catch (const std::exception& e) {
        printFromReplica("[REPLICA ERROR] execution failed: " + std::string(e.what()));
        throw;
    }
}

void auto_checkpoint_loop(BufferPool* bp) {
    while (true) {
        // Sleep for 5 minutes
        std::this_thread::sleep_for(std::chrono::minutes(5));
        
        {
            std::lock_guard<std::mutex> console_lock(console_mutex);
            std::cout << "\n[SYSTEM] Auto-Checkpoint triggered. Freezing transactions...\n" << std::flush;
        }

        // 1. Lock the database so no queries run mid-flush
        std::lock_guard<std::mutex> db_lock(global_db_lock);
        
        // 2. Flush dirty pages
        bp->checkpointFlush();
        
        // 3. Wipe the WAL clean
        std::ofstream clear_wal("wal.log", std::ios::trunc);
        clear_wal.close();

        {
            std::lock_guard<std::mutex> console_lock(console_mutex);
            std::cout << "[SYSTEM] Checkpoint complete. WAL truncated. Transactions unpaused.\nminidb-primary> " << std::flush;
        }
    }
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

    // Initialize database schema
    Schema schema = { {"id", ValueType::Int}, {"name", ValueType::Text} };
    students_table = catalog.createTable("students", schema, 0); // pk_index = 0

    if (mode == "primary") {
        is_primary = true;

        // 1. Run recovery from wal.log if it exists
        std::ifstream wal_check("wal.log");
        if (wal_check.is_open()) {
            std::cout << "[RECOVERY] wal.log found. Replaying WAL for crash recovery...\n";
            std::string log_line;
            int applied_count = 0;
            std::lock_guard<std::mutex> db_lock(global_db_lock);
            while (std::getline(wal_check, log_line)) {
                log_line = trim(log_line);
                if (log_line.empty()) continue;
                try {
                    execute_sql_statement(log_line);
                    applied_count++;
                } catch (const std::exception& e) {
                    std::cerr << "[RECOVERY ERROR] Failed to replay statement: " << log_line << " (" << e.what() << ")\n";
                }
            }
            std::cout << "[RECOVERY] Recovery complete. Applied " << applied_count << " log entries from WAL.\n\n";
            wal_check.close();

            // Clear WAL since the database state is now recovered and consistent
            std::ofstream clear_wal("wal.log", std::ios::trunc);
            clear_wal.close();
        }

        // 2. Initialize storage components for checkpointing
        std::unique_ptr<HeapFile> heap_file = std::make_unique<HeapFile>("students.db");
        std::unique_ptr<BufferPool> buffer_pool = std::make_unique<BufferPool>(10, heap_file.get());

        // 3. Spawn the automated checkpoint thread
        std::thread checkpoint_daemon(auto_checkpoint_loop, buffer_pool.get());
        checkpoint_daemon.detach(); // Let it run independently in the background

        // 4. Initialize log manager and replication node
        wal_manager = std::make_unique<LogManager>("wal.log");
        rep_node = std::make_unique<ReplicationNode>(ip, port);

        std::cout << "Database started in PRIMARY mode. Replicating to " << ip << ":" << port << "\n";
        std::cout << "Enter SQL queries (INSERT INTO students VALUES (id, 'name') | DELETE FROM students WHERE id = val | SELECT * FROM students | exit):\n\n";

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
                // 1. Write WAL
                wal_manager->writeLog(line);
                std::cout << "[WAL] Log entry written to disk.\n";

                // 2. Replicate to Replica
                std::cout << "[REPLICATION] Replicating statement to backup...\n";
                bool rep_ok = rep_node->sendLogToReplica(line);
                if (!rep_ok) {
                    std::cerr << "[ERROR] Replication failed! Timeout or connection lost. Aborting transaction.\n";
                    continue; // Skip executing locally to simulate abort
                }
                std::cout << "[REPLICATION] Replication success (Replica ACKed OK).\n";
            }

            // 3. Execute locally (Commit)
            try {
                std::lock_guard<std::mutex> db_lock(global_db_lock);
                execute_sql_statement(line);
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Execution error: " << e.what() << "\n";
            }
        }
    } 
    else if (mode == "replica") {
        is_primary = false;
        rep_node = std::make_unique<ReplicationNode>(ip, port);
        std::cout << "Database started in REPLICA mode. Listening on port " << port << "...\n";
        
        // 1. Spawn background thread for replica TCP listener
        std::thread background_listener([]() {
            try {
                rep_node->startReplicaServer(replica_execution_callback);
            } catch (const std::exception& e) {
                // Clean exit when stopReplicaServer is called
            }
        });

        // 2. Interactive CLI loop on the main thread
        std::string input_sql;
        while (true) {
            {
                std::lock_guard<std::mutex> lock(console_mutex);
                std::cout << "minidb-replica> " << std::flush;
            }
            if (!std::getline(std::cin, input_sql)) break;
            input_sql = trim(input_sql);
            if (input_sql == "exit" || input_sql == "quit") {
                break;
            }
            if (input_sql.empty()) continue;

            std::string upper_query = input_sql;
            std::transform(upper_query.begin(), upper_query.end(), upper_query.begin(), ::toupper);

            // CLI Bouncer for Read-Only replica status
            if (upper_query.rfind("INSERT", 0) == 0 || upper_query.rfind("DELETE", 0) == 0) {
                std::lock_guard<std::mutex> lock(console_mutex);
                std::cout << "[ERROR] Cannot execute write query on a read-only replica.\n";
                continue;
            }

            // Execute read-only SELECT query safely
            {
                std::lock_guard<std::mutex> db_lock(global_db_lock);
                try {
                    execute_sql_statement(input_sql);
                } catch (const std::exception& e) {
                    std::lock_guard<std::mutex> lock(console_mutex);
                    std::cout << "[ERROR] Execution error: " << e.what() << "\n";
                }
            }
        }

        // 3. Stop server and join background thread
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
