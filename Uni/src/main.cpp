#include "db.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <algorithm>

// ASCII table print helper
static void PrintOutputTable(const std::vector<std::string>& schema, const std::vector<Tuple>& rows) {
    if (schema.empty()) {
        std::cout << "(no columns)" << std::endl;
        return;
    }

    // Determine column widths
    std::vector<size_t> col_widths(schema.size());
    for (size_t i = 0; i < schema.size(); ++i) {
        col_widths[i] = schema[i].length();
    }
    for (const auto& row : rows) {
        for (size_t i = 0; i < schema.size() && i < row.values.size(); ++i) {
            col_widths[i] = std::max(col_widths[i], row.values[i].length());
        }
    }

    // Print divider
    auto print_divider = [&]() {
        for (size_t w : col_widths) {
            std::cout << "+" << std::string(w + 2, '-');
        }
        std::cout << "+" << std::endl;
    };

    print_divider();

    // Print header
    for (size_t i = 0; i < schema.size(); ++i) {
        std::cout << "| " << std::left << std::setw(col_widths[i]) << schema[i] << " ";
    }
    std::cout << "|" << std::endl;

    print_divider();

    // Print rows
    for (const auto& row : rows) {
        for (size_t i = 0; i < schema.size(); ++i) {
            std::string val = (i < row.values.size()) ? row.values[i] : "NULL";
            std::cout << "| " << std::left << std::setw(col_widths[i]) << val << " ";
        }
        std::cout << "|" << std::endl;
    }

    print_divider();
    std::cout << rows.size() << " row(s) returned." << std::endl;
}

static void PrintHelp() {
    std::cout << "\nAvailable commands:" << std::endl;
    std::cout << "  help                              - Show this help list" << std::endl;
    std::cout << "  begin                             - Start a new transaction" << std::endl;
    std::cout << "  commit                            - Commit the current transaction" << std::endl;
    std::cout << "  abort                             - Abort/Rollback the current transaction" << std::endl;
    std::cout << "  sql <statement>                   - Run SELECT, INSERT, or DELETE SQL statement" << std::endl;
    std::cout << "  show btree <table_name>           - Display the structure of a table's B+ Tree" << std::endl;
    std::cout << "  show cache                        - Show buffer pool hit/miss metrics" << std::endl;
    std::cout << "  crash                             - Simulate system crash (cold halt without flushing)" << std::endl;
    std::cout << "  recover                           - Restart MiniDB and trigger ARIES recovery" << std::endl;
    std::cout << "  exit                              - Exit the program" << std::endl;
}

int main() {
    std::cout << "=========================================================" << std::endl;
    std::cout << "   __  ___ _         _  ___   ___   ___                  " << std::endl;
    std::cout << "  /  |/  /(_) ___   (_)/ _ \\ / _ ) / _ \\                 " << std::endl;
    std::cout << " / /|_/ // / / _ \\ / // // // _  |/ ___/                 " << std::endl;
    std::cout << "/_/  /_//_/_//_//_//_//____//____//_/                     " << std::endl;
    std::cout << "   Advanced DBMS Capstone Project — Relational DB Engine " << std::endl;
    std::cout << "=========================================================" << std::endl;

    std::cout << "\nSelect Concurrency Control Engine:" << std::endl;
    std::cout << "  1. Strict Two-Phase Locking (2PL)" << std::endl;
    std::cout << "  2. Multi-Version Concurrency Control (MVCC) [Track B Extension]" << std::endl;
    std::cout << "Enter Choice (1 or 2): ";
    
    int choice = 1;
    if (!(std::cin >> choice)) {
        choice = 1;
    }
    std::cin.ignore(1000, '\n');

    ConcurrencyMode mode = (choice == 2) ? ConcurrencyMode::MULTI_VERSION_CONCURRENCY_CONTROL : ConcurrencyMode::TWO_PHASE_LOCKING;
    std::string mode_str = (mode == ConcurrencyMode::MULTI_VERSION_CONCURRENCY_CONTROL) ? "MVCC" : "Strict 2PL";
    std::cout << "[MiniDB] Starting engine in " << mode_str << " mode..." << std::endl;

    std::string db_file = "minidb.db";
    std::string log_file = "minidb.log";

    // Recreate files for a clean run
    {
        std::ofstream d(db_file, std::ios::trunc);
        std::ofstream l(log_file, std::ios::trunc);
    }

    std::unique_ptr<Database> db = std::make_unique<Database>(db_file, log_file, mode);

    // Initialize Schema catalog
    db->CreateTable("users", {"users.id", "users.name", "users.age"});
    db->CreateTable("orders", {"orders.id", "orders.user_id", "orders.product"});

    std::cout << "[MiniDB] Created table 'users' (id, name, age) with Primary B+ Tree Index" << std::endl;
    std::cout << "[MiniDB] Created table 'orders' (id, user_id, product) with Primary B+ Tree Index" << std::endl;

    Transaction* active_txn = nullptr;

    PrintHelp();

    std::string line;
    while (true) {
        if (active_txn) {
            std::cout << "\n[T" << active_txn->txn_id << "] minidb> ";
        } else {
            std::cout << "\n[No Tx] minidb> ";
        }

        if (!std::getline(std::cin, line)) {
            break;
        }

        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string cmd;
        ss >> cmd;

        // Convert command to lowercase
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

        if (cmd == "exit") {
            break;
        }
        else if (cmd == "help") {
            PrintHelp();
        }
        else if (cmd == "begin") {
            if (active_txn) {
                std::cout << "[Warn] Transaction T" << active_txn->txn_id << " is already active in this terminal." << std::endl;
            } else {
                active_txn = db->BeginTransaction();
                std::cout << "[Tx Started] Began transaction T" << active_txn->txn_id << std::endl;
            }
        }
        else if (cmd == "commit") {
            if (!active_txn) {
                std::cout << "[SQL Error] No active transaction. Type 'begin' first." << std::endl;
            } else {
                bool ok = db->CommitTransaction(active_txn);
                if (ok) {
                    std::cout << "[Commit Success] Transaction committed." << std::endl;
                } else {
                    std::cout << "[Commit Failed] Transaction was aborted (e.g. rolled back due to deadlock)." << std::endl;
                }
                active_txn = nullptr;
            }
        }
        else if (cmd == "abort") {
            if (!active_txn) {
                std::cout << "[SQL Error] No active transaction." << std::endl;
            } else {
                db->AbortTransaction(active_txn);
                std::cout << "[Abort Rollback] Transaction rolled back." << std::endl;
                active_txn = nullptr;
            }
        }
        else if (cmd == "sql") {
            std::string sql;
            std::getline(ss, sql);
            
            // Trim leading/trailing whitespace
            size_t first = sql.find_first_not_of(" ");
            if (first != std::string::npos) {
                sql = sql.substr(first);
            }

            if (sql.empty()) {
                std::cout << "[SQL Error] Missing SQL statement." << std::endl;
                continue;
            }

            bool auto_commit = false;
            if (!active_txn) {
                active_txn = db->BeginTransaction();
                auto_commit = true;
            }

            std::vector<Tuple> output_rows;
            std::vector<std::string> output_schema;
            bool ok = db->ExecuteSQL(active_txn, sql, output_rows, output_schema);

            if (ok) {
                if (output_rows.empty() && output_schema.empty()) {
                    // Modification query
                    if (auto_commit) {
                        db->CommitTransaction(active_txn);
                        active_txn = nullptr;
                    }
                } else {
                    // Select query
                    PrintOutputTable(output_schema, output_rows);
                    if (auto_commit) {
                        db->CommitTransaction(active_txn);
                        active_txn = nullptr;
                    }
                }
            } else {
                std::cout << "[SQL Failed] Query execution failed or transaction was aborted." << std::endl;
                if (active_txn->state == TransactionState::ABORTED || auto_commit) {
                    db->AbortTransaction(active_txn);
                    active_txn = nullptr;
                }
            }
        }
        else if (cmd == "show") {
            std::string sub_cmd;
            ss >> sub_cmd;
            std::transform(sub_cmd.begin(), sub_cmd.end(), sub_cmd.begin(), ::tolower);
            if (sub_cmd == "btree") {
                std::string tbl;
                ss >> tbl;
                db->PrintBPlusTree(tbl);
            } else if (sub_cmd == "cache") {
                db->PrintBufferPoolStats();
            } else {
                std::cout << "[Error] Usage: show btree <tbl_name> | show cache" << std::endl;
            }
        }
        else if (cmd == "crash") {
            if (active_txn) {
                std::cout << "[Crash] Aborting T" << active_txn->txn_id << " in memory..." << std::endl;
                active_txn = nullptr;
            }
            db->SimulateCrash();
            db.reset(); // Destroy DB state completely
        }
        else if (cmd == "recover") {
            if (db) {
                std::cout << "[Error] Cannot recover while database is running. Run 'crash' first." << std::endl;
            } else {
                db = std::make_unique<Database>(db_file, log_file, mode);
                db->CreateTable("users", {"users.id", "users.name", "users.age"});
                db->CreateTable("orders", {"orders.id", "orders.user_id", "orders.product"});
                db->RestartAndRecover();
            }
        }
        else {
            std::cout << "[Error] Unknown command. Type 'help' to see options." << std::endl;
        }
    }

    std::cout << "Exiting MiniDB CLI Shell. Goodbye!" << std::endl;
    return 0;
}
