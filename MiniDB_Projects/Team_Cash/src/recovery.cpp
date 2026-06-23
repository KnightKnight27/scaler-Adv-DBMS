#include "recovery.h"

#include <fstream>
#include <sstream>
#include <vector>

namespace minidb {

LogManager::LogManager(const std::string& path) : path_(path) {}

// Each call appends one record and flushes, so the log is always ahead of the
// data on disk (the write-ahead rule).
void LogManager::logBegin(int txn) {
    std::ofstream f(path_, std::ios::app);
    f << "BEGIN " << txn << "\n";
}

void LogManager::logUpdate(int txn, int key, const std::string& oldVal, const std::string& newVal) {
    std::ofstream f(path_, std::ios::app);
    f << "UPDATE " << txn << " " << key << " " << oldVal << " " << newVal << "\n";
}

void LogManager::logCommit(int txn) {
    std::ofstream f(path_, std::ios::app);
    f << "COMMIT " << txn << "\n";
}

void LogManager::flush() { /* each write already flushes by closing the stream */ }

std::map<int, std::string> RecoveryManager::recover(int& redone, int& undone) {
    redone = 0;
    undone = 0;

    struct Update { int txn, key; std::string oldVal, newVal; };
    std::vector<Update> updates;
    std::map<int, bool> committed;  // txn -> reached COMMIT

    std::ifstream f(logPath_);
    std::string type;
    while (f >> type) {
        if (type == "BEGIN") {
            int txn; f >> txn; committed[txn] = false;
        } else if (type == "UPDATE") {
            Update u; f >> u.txn >> u.key >> u.oldVal >> u.newVal;
            updates.push_back(u);
        } else if (type == "COMMIT") {
            int txn; f >> txn; committed[txn] = true;
        }
    }

    std::map<int, std::string> store;
    // Redo: replay committed updates in order (winners).
    for (const Update& u : updates) {
        if (committed[u.txn]) { store[u.key] = u.newVal; redone++; }
    }
    // Undo: roll back uncommitted updates in reverse, restoring before-images.
    for (auto it = updates.rbegin(); it != updates.rend(); ++it) {
        if (!committed[it->txn]) {
            if (it->oldVal == "~") store.erase(it->key);  // "~" = the key did not exist
            else store[it->key] = it->oldVal;
            undone++;
        }
    }
    return store;
}

}  // namespace minidb
