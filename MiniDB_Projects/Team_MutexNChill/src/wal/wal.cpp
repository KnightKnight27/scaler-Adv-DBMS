#include "wal.h"
#include <stdexcept>
#include <cstring>
#include <sstream>
#include <iostream>
#include <fstream>

WalManager::WalManager(const std::string& log_filename) : filename(log_filename) {
    // Open in append mode so previous records survive across restarts.
    log_file = fopen(filename.c_str(), "a");
    if (!log_file) {
        throw std::runtime_error("Cannot open WAL file: " + filename);
    }
}

WalManager::~WalManager() {
    if (log_file) fclose(log_file);
}

void WalManager::writeLine(const std::string& line) {
    fputs((line + "\n").c_str(), log_file);
    fflush(log_file); // force to disk before returning
}

void WalManager::logBegin(int txn_id) {
    writeLine("BEGIN " + std::to_string(txn_id));
}

void WalManager::logInsert(int txn_id, const std::string& table, const Row& row) {
    // Format: INSERT txn_id table id name age extra
    std::string line = "INSERT " + std::to_string(txn_id) + " " + table +
                       " " + std::to_string(row.id) +
                       " " + std::string(row.name) +
                       " " + std::to_string(row.age) +
                       " " + std::to_string(row.extra);
    writeLine(line);
}

void WalManager::logDelete(int txn_id, const std::string& table, int row_id) {
    writeLine("DELETE " + std::to_string(txn_id) + " " + table + " " + std::to_string(row_id));
}

void WalManager::logCommit(int txn_id) {
    writeLine("COMMIT " + std::to_string(txn_id));
}

void WalManager::logAbort(int txn_id) {
    writeLine("ABORT " + std::to_string(txn_id));
}

void WalManager::simulateCrash() {
    // Close the log, reopen it, and truncate just before the last line.
    // This mimics the OS killing the process mid-write.
    fclose(log_file);
    log_file = nullptr;

    // Read all existing lines.
    std::ifstream in(filename);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    in.close();

    // Drop the last line (incomplete / uncommitted write).
    if (!lines.empty()) lines.pop_back();

    // Rewrite without the last record.
    std::ofstream out(filename, std::ios::trunc);
    for (auto& l : lines) out << l << "\n";
    out.close();

    // Reopen for appending.
    log_file = fopen(filename.c_str(), "a");
}

int WalManager::recover(std::map<std::string, HeapFile*>& heaps,
                         std::map<std::string, BPlusTree*>& indexes) {
    fclose(log_file);
    log_file = nullptr;

    // Pass 1: find all committed transaction IDs.
    std::set<int> committed;
    {
        std::ifstream in(filename);
        std::string line;
        while (std::getline(in, line)) {
            if (line.substr(0, 6) == "COMMIT") {
                int txn_id = std::stoi(line.substr(7));
                committed.insert(txn_id);
            }
        }
    }

    // Pass 2: redo all INSERT/DELETE from committed transactions.
    int redone = 0;
    {
        std::ifstream in(filename);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            std::istringstream ss(line);
            std::string op;
            ss >> op;

            if (op == "INSERT") {
                int txn_id; std::string table;
                ss >> txn_id >> table;
                if (committed.count(txn_id)) {
                    Row row;
                    memset(&row, 0, sizeof(Row));
                    std::string name_str;
                    ss >> row.id >> name_str >> row.age >> row.extra;
                    strncpy(row.name, name_str.c_str(), 31);
                    row.name[31] = '\0';
                    row.is_valid = true;
                    if (heaps.count(table)) {
                        // Only redo if not already present (idempotency check).
                        RID existing;
                        if (!indexes[table]->search(row.id, existing)) {
                            RID rid = heaps[table]->insertRow(row);
                            indexes[table]->insert(row.id, rid);
                            redone++;
                        }
                    }
                }
            } else if (op == "DELETE") {
                int txn_id; std::string table; int row_id;
                ss >> txn_id >> table >> row_id;
                if (committed.count(txn_id) && indexes.count(table)) {
                    RID rid;
                    if (indexes[table]->search(row_id, rid)) {
                        heaps[table]->deleteRow(rid);
                        indexes[table]->remove(row_id);
                        redone++;
                    }
                }
            }
        }
    }

    // Reopen for appending (normal operation resumes after recovery).
    log_file = fopen(filename.c_str(), "a");
    return redone;
}

void WalManager::printLog() {
    fflush(log_file);
    std::ifstream in(filename);
    std::string line;
    while (std::getline(in, line)) {
        std::cout << "  " << line << "\n";
    }
}
