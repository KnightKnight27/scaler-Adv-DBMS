#pragma once
#include "../storage/heap_file.h"
#include "../index/bplus_tree.h"
#include <string>
#include <set>
#include <map>

// Write-Ahead Logging (WAL)
//
// Rule: every change is logged to disk BEFORE the page is modified.
// This guarantees that after a crash we can redo committed work and
// discard uncommitted work.
//
// Log format (plain text, one record per line, easy to inspect):
//   BEGIN   <txn_id>
//   INSERT  <txn_id> <table> <id> <name> <age> <extra>
//   DELETE  <txn_id> <table> <id>
//   COMMIT  <txn_id>
//   ABORT   <txn_id>
class WalManager {
public:
    explicit WalManager(const std::string& log_filename);
    ~WalManager();

    void logBegin(int txn_id);
    void logInsert(int txn_id, const std::string& table, const Row& row);
    void logDelete(int txn_id, const std::string& table, int row_id);
    void logCommit(int txn_id);
    void logAbort(int txn_id);

    // Simulate a crash by truncating the log at an arbitrary point.
    // This leaves the last record incomplete (uncommitted transaction).
    void simulateCrash();

    // Read the log and replay all committed transactions into the
    // provided heap files and indexes.
    // Returns the number of rows redone.
    int recover(std::map<std::string, HeapFile*>& heaps,
                std::map<std::string, BPlusTree*>& indexes);

    // Print the raw log contents to stdout (for demo).
    void printLog();

private:
    std::string filename;
    FILE*       log_file;

    void writeLine(const std::string& line);
};
