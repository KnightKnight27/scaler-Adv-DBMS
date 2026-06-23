#pragma once

#include "query/parser.h"
#include "query/optimizer.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"
#include "transaction/tx_manager.h"
#include "recovery/wal.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <memory>
#include <iostream>

// ─── TableEntry ───────────────────────────────────────────────────────────────
// Everything MiniDB knows about a table.

struct TableEntry {
    std::unique_ptr<HeapFile>  heap;    // storage layer
    std::unique_ptr<BPlusTree> index;   // primary key B+ tree index
};

// ─── Executor ─────────────────────────────────────────────────────────────────
//
// The Executor receives a ParsedQuery and runs it against the database state.
// It implements the Volcano / iterator model:
//   1. TableScan  — iterates all records in the HeapFile
//   2. IndexScan  — calls BPlusTree::search() for a specific key
//   3. NestedLoopJoin — outer table scan × inner index lookup
//
// The Optimizer is consulted for SELECT to choose between scan strategies.
//
// All write operations (INSERT, DELETE) go through the WAL and TxManager.

class Executor {
public:
    Executor(BufferPool& bp, WAL& wal, TxManager& txm);

    // Execute a parsed query. Prints results to stdout.
    void execute(const ParsedQuery& q);

    // Register a table (used on startup or after CREATE TABLE).
    void registerTable(const std::string& name, TableEntry entry);

    // Does a table with this name exist?
    bool tableExists(const std::string& name) const;

    // Get pointers to a table's heap and index (for recovery/replication)
    std::pair<HeapFile*, BPlusTree*> getTablePointers(const std::string& name);

    // Print all registered table names.
    void showTables() const;

private:
    // ── Command handlers ─────────────────────────────────────────────────────

    void doCreateTable(const ParsedQuery& q);
    void doInsert(const ParsedQuery& q);
    void doSelectAll(const ParsedQuery& q);
    void doSelectKey(const ParsedQuery& q);
    void doSelectRange(const ParsedQuery& q);
    void doJoin(const ParsedQuery& q);
    void doDelete(const ParsedQuery& q);

    // ── Helpers ──────────────────────────────────────────────────────────────

    // Print one record to stdout.
    void printRecord(const RecordID& rid, const Record& rec) const;

    // Get a table entry or print an error and return nullptr.
    TableEntry* getTable(const std::string& name);

    // ── Members ──────────────────────────────────────────────────────────────

    BufferPool&   bp_;
    WAL&          wal_;
    TxManager&    txm_;
    Optimizer     optimizer_;

    // All tables in the database: name → TableEntry
    std::unordered_map<std::string, TableEntry> tables_;

    // Current active transaction (0 = auto-commit mode)
    TxID current_txid_ = 0;
};
