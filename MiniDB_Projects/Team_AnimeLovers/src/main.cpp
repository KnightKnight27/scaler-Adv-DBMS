#include "engine.h"
#include "transaction.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

// Pretty-print a ResultSet as an ASCII table
static void print_result(const ResultSet& rs) {
    if (rs.columns.empty() && rs.rows.empty()) {
        std::cout << "OK\n";
        return;
    }
    // Compute column widths
    std::vector<size_t> widths;
    for (auto& c : rs.columns) widths.push_back(c.size());
    for (auto& row : rs.rows) {
        for (size_t i = 0; i < row.size() && i < widths.size(); ++i)
            widths[i] = std::max(widths[i], row[i].to_string().size());
    }

    auto sep = [&]() {
        std::cout << '+';
        for (auto w : widths) std::cout << std::string(w + 2, '-') << '+';
        std::cout << '\n';
    };
    sep();
    std::cout << '|';
    for (size_t i = 0; i < rs.columns.size(); ++i)
        std::cout << ' ' << std::left << std::setw((int)widths[i])
                  << rs.columns[i] << " |";
    std::cout << '\n';
    sep();
    for (auto& row : rs.rows) {
        std::cout << '|';
        for (size_t i = 0; i < row.size() && i < widths.size(); ++i)
            std::cout << ' ' << std::left << std::setw((int)widths[i])
                      << row[i].to_string() << " |";
        std::cout << '\n';
    }
    sep();
    std::cout << rs.rows.size() << " row(s)\n";
}

// Execute one SQL statement through the transaction manager and print result.
// Handles BEGIN / COMMIT / ROLLBACK by managing the active txn_id.
static void run_sql(TransactionManager& tm, TxnId& active_txn,
                    const std::string& sql, bool verbose) {
    std::string upper = sql;
    for (auto& c : upper) c = std::toupper(c);
    // Strip leading/trailing spaces from the upper version for comparison
    size_t s = upper.find_first_not_of(" \t\r\n");
    std::string trimmed = (s == std::string::npos) ? "" : upper.substr(s);

    if (trimmed.empty() || trimmed[0] == '-') return; // blank / comment

    try {
        if (trimmed.rfind("BEGIN", 0) == 0) {
            if (active_txn != 0)
                std::cout << "ERROR: transaction already active\n";
            else { active_txn = tm.begin(); if (verbose) std::cout << "BEGIN txn=" << active_txn << "\n"; }
            return;
        }
        if (trimmed.rfind("COMMIT", 0) == 0) {
            if (active_txn == 0) { std::cout << "ERROR: no active transaction\n"; return; }
            tm.commit(active_txn);
            if (verbose) std::cout << "COMMIT txn=" << active_txn << "\n";
            active_txn = 0;
            return;
        }
        if (trimmed.rfind("ROLLBACK", 0) == 0) {
            if (active_txn == 0) { std::cout << "ERROR: no active transaction\n"; return; }
            tm.abort(active_txn);
            if (verbose) std::cout << "ROLLBACK txn=" << active_txn << "\n";
            active_txn = 0;
            return;
        }

        // Auto-begin if no transaction is open (autocommit mode)
        bool autocommit = (active_txn == 0);
        if (autocommit) active_txn = tm.begin();

        ResultSet rs = tm.execute(active_txn, sql);
        print_result(rs);

        if (autocommit) { tm.commit(active_txn); active_txn = 0; }

    } catch (const DeadlockException& e) {
        std::cout << "DEADLOCK detected — transaction " << e.victim << " aborted\n";
        if (active_txn != 0) { tm.abort(active_txn); active_txn = 0; }
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
        if (active_txn != 0) { tm.abort(active_txn); active_txn = 0; }
    }
}

// Run all statements from a .sql file
static void batch_mode(TransactionManager& tm, const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f) { std::cerr << "Cannot open file: " << filepath << "\n"; return; }
    std::string line, statement;
    TxnId active = 0;
    while (std::getline(f, line)) {
        statement += line + " ";
        // Execute on semicolon boundary
        if (line.find(';') != std::string::npos) {
            run_sql(tm, active, statement, false);
            statement.clear();
        }
    }
    if (!statement.empty() && statement.find_first_not_of(" \t\r\n") != std::string::npos)
        run_sql(tm, active, statement, false);
}

static void print_usage() {
    std::cout <<
        "MiniDB — Team AnimeLovers\n\n"
        "Usage:\n"
        "  minidb [--data <dir>] [--mvcc] [--batch <file>]\n\n"
        "Options:\n"
        "  --data <dir>    database directory (default: minidb_data)\n"
        "  --mvcc          use MVCC concurrency control (default: 2PL)\n"
        "  --batch <file>  run SQL from file and exit\n\n"
        "REPL commands:\n"
        "  Any SQL statement terminated by ';'\n"
        "  \\q or exit     quit\n\n";
}

int main(int argc, char* argv[]) {
    std::string db_dir    = "minidb_data";
    std::string batch_file;
    ConcurrencyMode mode  = ConcurrencyMode::TWO_PL;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--data" && i+1 < argc)       { db_dir = argv[++i]; }
        else if (arg == "--mvcc")                 { mode = ConcurrencyMode::MVCC; }
        else if (arg == "--batch" && i+1 < argc)  { batch_file = argv[++i]; }
        else if (arg == "--help")                 { print_usage(); return 0; }
    }

    fs::create_directories(db_dir);
    std::string wal_path = db_dir + "/minidb.wal";

    // Run crash recovery before opening the database
    {
        Database tmp(db_dir);
        recover(tmp, wal_path);
    }

    Database db(db_dir);
    TransactionManager tm(db, wal_path, mode);

    std::cout << "MiniDB ready. Mode: "
              << (mode == ConcurrencyMode::MVCC ? "MVCC" : "2PL") << "\n";

    if (!batch_file.empty()) {
        batch_mode(tm, batch_file);
        return 0;
    }

    // Interactive REPL
    std::cout << "Type SQL statements (end with ';'). \\q to quit.\n\n";
    std::string line, statement;
    TxnId active = 0;
    while (true) {
        std::cout << (active ? "minidb*> " : "minidb> ");
        if (!std::getline(std::cin, line)) break;
        if (line == "\\q" || line == "exit" || line == "quit") break;
        statement += line + " ";
        if (line.find(';') != std::string::npos) {
            run_sql(tm, active, statement, true);
            statement.clear();
        }
    }
    if (active != 0) {
        std::cout << "Rolling back open transaction " << active << "\n";
        tm.abort(active);
    }
    return 0;
}
