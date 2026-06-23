#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <memory>
#include <algorithm>
#include "wal.h"
#include "replication.h"
#include "execution.h"

using namespace minidb;

// Globals for DB state in main.cpp
Catalog catalog;
Table* students_table = nullptr;
std::unique_ptr<LogManager> wal_manager;
std::unique_ptr<ReplicationNode> rep_node;
bool is_primary = true;

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

// Replica node callback wrapper
void replica_execution_callback(const std::string& sql) {
    std::cout << "\n[REPLICA] Received replication stream event: " << sql << "\n";
    execute_sql_statement(sql);
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
        
        rep_node->startReplicaServer(replica_execution_callback);
    } 
    else {
        std::cerr << "Invalid mode: " << mode << ". Use 'primary' or 'replica'.\n";
        return 1;
    }

    return 0;
}
