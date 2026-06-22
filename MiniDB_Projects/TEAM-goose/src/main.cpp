#include "database.h"
#include <iostream>
#include <sstream>
#include <readline/readline.h>
#include <readline/history.h>

using namespace minidb;

// minidb cli — interactive shell

static void print_banner() {
    std::cout << "\n"
              << "╔══════════════════════════════════════════════╗\n"
              << "║           MiniDB v1.0 — Team Goose           ║\n"
              << "║   LSM-tree Storage Engine (Track C)           ║\n"
              << "╚══════════════════════════════════════════════╝\n"
              << "\n"
              << "Type SQL statements or special commands:\n"
              << "  BEGIN      — start a transaction\n"
              << "  COMMIT     — commit current transaction\n"
              << "  ROLLBACK   — abort current transaction\n"
              << "  LOCKS      — show lock table state\n"
              << "  STATS      — show storage statistics\n"
              << "  EXIT/QUIT  — exit\n"
              << "\n";
}

static void print_stats(Database& db) {
    std::cout << "\n--- Storage Statistics ---\n";
    std::cout << "MemTable size:   " << db.storage().memtable_size() << " entries\n";
    std::cout << "SSTable count:   " << db.storage().sstable_count() << "\n";
    std::cout << "Data directory:  " << db.storage().data_dir() << "\n";
    std::cout << "Tables:          ";
    for (const auto& [name, meta] : db.catalog().tables()) {
        std::cout << name << " (" << meta.row_count << " rows) ";
    }
    std::cout << "\n\n";
}

static std::string get_input() {
    char* line = readline("minidb> ");
    if (!line) return "";
    std::string input(line);
    free(line);
    if (!input.empty()) add_history(input.c_str());
    return input;
}

int main(int argc, char* argv[]) {
    std::string data_dir = "minidb_data";
    if (argc > 1) data_dir = argv[1];

    Database db(data_dir);

    if (!db.init(true)) {
        std::cerr << "Failed to initialize database.\n";
        return 1;
    }

    print_banner();

    std::string buffer;
    bool running = true;

    while (running) {
        std::string prompt;
        if (db.current_txn() != INVALID_TXN) {
            prompt = "minidb[TXN" + std::to_string(db.current_txn()) + "]> ";
        } else {
            prompt = "minidb> ";
        }

        char* line = readline(prompt.c_str());
        if (!line) break;
        std::string input(line);
        free(line);

        if (input.empty()) continue;
        add_history(input.c_str());

        // accumulate multi-line input (ends with ;)
        buffer += input;
        if (buffer.back() != ';' && buffer.find("EXIT") == std::string::npos &&
            buffer.find("QUIT") == std::string::npos) {
            buffer += " ";
            continue;
        }

        std::string cmd = buffer;
        // remove trailing semicolon for special commands
        if (!cmd.empty() && cmd.back() == ';') {
            cmd.pop_back();
        }
        buffer.clear();

        // special commands
        std::string upper = to_upper(cmd);
        if (upper == "EXIT" || upper == "QUIT") {
            running = false;
            std::cout << "Bye!\n";
            break;
        }

        if (upper == "BEGIN") {
            TxnID id = db.begin_transaction();
            std::cout << "Transaction " << id << " started.\n";
            continue;
        }

        if (upper == "COMMIT") {
            if (db.current_txn() == INVALID_TXN) {
                std::cout << "No active transaction.\n";
                continue;
            }
            TxnID id = db.current_txn();
            bool ok = db.commit_transaction(id);
            std::cout << (ok ? "Transaction " + std::to_string(id) + " committed.\n"
                            : "Commit failed.\n");
            continue;
        }

        if (upper == "ROLLBACK") {
            if (db.current_txn() == INVALID_TXN) {
                std::cout << "No active transaction.\n";
                continue;
            }
            TxnID id = db.current_txn();
            bool ok = db.abort_transaction(id);
            std::cout << (ok ? "Transaction " + std::to_string(id) + " rolled back.\n"
                            : "Rollback failed.\n");
            continue;
        }

        if (upper == "LOCKS") {
            std::cout << db.lock_manager().dump();
            continue;
        }

        if (upper == "STATS") {
            print_stats(db);
            continue;
        }

        // execute sql
        try {
            std::string result = db.execute(cmd + ";");
            std::cout << result << "\n";
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }

    db.shutdown();
    return 0;
}
