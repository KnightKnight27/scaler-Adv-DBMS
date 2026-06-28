#include <filesystem>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>

#include "minidb/buffer/buffer_pool.h"
#include "minidb/execution/execution_engine.h"
#include "minidb/storage/disk_manager.h"
#include "minidb/transaction/mvcc.h"
#include "minidb/transaction/transaction_manager.h"

using namespace minidb;

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (tokenStream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

void PrintHeader(const std::string& title) {
    std::cout << "\n=======================================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "=======================================================\n";
}

int main() {
    std::cout << "Starting MiniDB Interactive Demo...\n";
    
    // --- Phase 1: M1-M3 ---
    PrintHeader("Phase 1: M1-M3 Query Execution (SQL)");
    auto path = std::filesystem::temp_directory_path() / "minidb_demo.db";
    std::filesystem::remove(path);
    DiskManager disk(path);
    BufferPool buffer(disk, 4);
    ExecutionEngine engine(buffer);
    
    std::cout << "[System] Initializing table 'users(id, name)'...\n";
    engine.CreateTable("users", {Column{"id", ColumnType::Text, true}, Column{"name", ColumnType::Text, false}});
    std::cout << "Type SQL queries (e.g. INSERT INTO users VALUES ('1', 'Ada');)\n";
    std::cout << "Type 'next' to move to Phase 2.\n\n";

    std::string line;
    while (true) {
        std::cout << "SQL> ";
        if (!std::getline(std::cin, line)) break;
        if (line == "next") break;
        if (line.empty()) continue;

        try {
            auto result = engine.Execute(line);
            if (!result.columns.empty()) {
                for (const auto& col : result.columns) std::cout << col << "\t";
                std::cout << "\n";
                for (const auto& row : result.rows) {
                    for (const auto& val : row) std::cout << val << "\t";
                    std::cout << "\n";
                }
            } else {
                std::cout << "Success (Affected rows: " << result.affected_rows << ")\n";
            }
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }

    // --- Phase 2: M4 ---
    PrintHeader("Phase 2: M4 Transactions (Strict 2PL)");
    std::cout << "Commands: BEGIN <name>, SHARE <name> <res>, EXCLUSIVE <name> <res>, COMMIT <name>\n";
    std::cout << "Type 'next' to move to Phase 3.\n\n";

    TransactionManager tx_mgr;
    std::unordered_map<std::string, Transaction*> tx_map;

    while (true) {
        std::cout << "2PL> ";
        if (!std::getline(std::cin, line)) break;
        if (line == "next") break;
        if (line.empty()) continue;

        auto tokens = split(line);
        if (tokens.empty()) continue;
        std::string cmd = tokens[0];

        try {
            if (cmd == "BEGIN" && tokens.size() == 2) {
                tx_map[tokens[1]] = &tx_mgr.Begin();
                std::cout << "Started transaction " << tokens[1] << "\n";
            } else if (cmd == "SHARE" && tokens.size() == 3) {
                bool granted = tx_mgr.LockShared(tx_map[tokens[1]]->id, tokens[2]);
                std::cout << (granted ? "GRANTED" : "DENIED") << "\n";
            } else if (cmd == "EXCLUSIVE" && tokens.size() == 3) {
                bool granted = tx_mgr.LockExclusive(tx_map[tokens[1]]->id, tokens[2]);
                std::cout << (granted ? "GRANTED" : "WAITING (Blocked)") << "\n";
            } else if (cmd == "COMMIT" && tokens.size() == 2) {
                tx_mgr.Commit(tx_map[tokens[1]]->id);
                std::cout << "Committed transaction " << tokens[1] << "\n";
            } else {
                std::cout << "Unknown or malformed command.\n";
            }
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }

    // --- Phase 3: M6 MVCC ---
    PrintHeader("Phase 3: M6 Track B (MVCC)");
    std::cout << "Operating on a single row with fixed ID for simplicity.\n";
    std::cout << "Commands: BEGIN <name>, INSERT <name> <val>, UPDATE <name> <val>, READ <name>, COMMIT <name>, ABORT <name>\n";
    std::cout << "Type 'exit' to end demo.\n\n";

    MvccStore mvcc;
    std::unordered_map<std::string, MvccTransaction*> mvcc_map;
    Rid rid{1, 0};

    while (true) {
        std::cout << "MVCC> ";
        if (!std::getline(std::cin, line)) break;
        if (line == "exit" || line == "quit") break;
        if (line.empty()) continue;

        auto tokens = split(line);
        if (tokens.empty()) continue;
        std::string cmd = tokens[0];

        try {
            if (cmd == "BEGIN" && tokens.size() == 2) {
                mvcc_map[tokens[1]] = &mvcc.Begin();
                std::cout << "Started transaction " << tokens[1] << "\n";
            } else if (cmd == "INSERT" && tokens.size() == 3) {
                bool ok = mvcc.Insert(mvcc_map[tokens[1]]->id, rid, EncodeRow({"1", tokens[2]}));
                std::cout << (ok ? "Inserted" : "Failed") << "\n";
            } else if (cmd == "UPDATE" && tokens.size() == 3) {
                bool ok = mvcc.Update(mvcc_map[tokens[1]]->id, rid, EncodeRow({"1", tokens[2]}));
                std::cout << (ok ? "Updated" : "Write-Write Conflict! Blocked or Aborted") << "\n";
            } else if (cmd == "READ" && tokens.size() == 2) {
                auto snap = mvcc.Read(mvcc_map[tokens[1]]->id, rid);
                if (snap.has_value()) {
                    std::cout << "Read Snapshot Value: '" << DecodeRow(*snap)[1] << "'\n";
                } else {
                    std::cout << "Not Found (No visible snapshot)\n";
                }
            } else if (cmd == "COMMIT" && tokens.size() == 2) {
                mvcc.Commit(mvcc_map[tokens[1]]->id);
                std::cout << "Committed transaction " << tokens[1] << "\n";
            } else if (cmd == "ABORT" && tokens.size() == 2) {
                mvcc.Abort(mvcc_map[tokens[1]]->id);
                std::cout << "Aborted transaction " << tokens[1] << "\n";
            } else {
                std::cout << "Unknown or malformed command.\n";
            }
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }

    std::cout << "Demo complete. Goodbye!\n";
    return 0;
}
