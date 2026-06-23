// MiniDB - Write-Ahead Log records.
//
// The WAL is a logical redo/undo log: it records DDL (CREATE TABLE/INDEX) and the row image of
// every INSERT/DELETE, tagged with the transaction that did it, plus BEGIN/COMMIT/ABORT markers.
// Because the log is self-describing, replaying it from the start fully reconstructs the
// database. A transaction's effects are kept only if it committed (the WAL rule: log first,
// then a crash before COMMIT means the work is discarded).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../common/schema.h"
#include "../common/types.h"

namespace minidb {

enum class LogType : uint8_t { Begin, Commit, Abort, CreateTable, CreateIndex, Insert, Delete };

struct LogRecord {
    LogType type;
    int txn_id = 0;                 // 0 == autocommit (immediately committed)
    std::string table;              // DDL/DML target
    std::vector<Value> row;         // Insert/Delete row image
    std::vector<Column> columns;    // CreateTable schema
    std::string column;             // CreateIndex column
    bool unique = false;            // CreateIndex flag

    static LogRecord Begin(int txn)  { return {LogType::Begin,  txn, {}, {}, {}, {}, false}; }
    static LogRecord Commit(int txn) { return {LogType::Commit, txn, {}, {}, {}, {}, false}; }
    static LogRecord Abort(int txn)  { return {LogType::Abort,  txn, {}, {}, {}, {}, false}; }
};

// Length-prefixed binary (de)serialization of a single record.
std::string SerializeRecord(const LogRecord& r);
// Parse one record starting at offset `pos` in `buf`; advances pos. Returns false at end.
bool DeserializeRecord(const std::string& buf, size_t& pos, LogRecord* out);

}  // namespace minidb
