// Write-Ahead Log (WAL).
//
// MiniDB logs *logical*, primary-key-keyed operations. Each record is appended
// to an append-only file as [u32 len][u8 type][i64 txn][payload]. The log is
// the durable ground truth: pages may be written lazily (no-force) and even
// while a txn is uncommitted (steal), because recovery can both redo committed
// work and undo uncommitted work from these records.
//
// Recovery (two passes, ARIES-style but logical):
//   1. Analysis  -- find the set of committed transactions.
//   2. Redo      -- replay every data op in order as an idempotent upsert/delete
//                   (brings the database to its crash-time state).
//   3. Undo      -- for transactions that never committed, reverse their ops
//                   using the before-images stored in the log.
//
// Because operations are keyed by primary key (not physical RID) and applied as
// upserts, redo is idempotent and safe to run over a partially-flushed data file.
#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "common/types.hpp"

namespace minidb {

enum class LogType : uint8_t {
    BEGIN = 1, COMMIT, ABORT, INSERT, UPDATE, DELETE, CREATE_TABLE, CHECKPOINT
};

struct LogRecord {
    LogType type;
    TxnId   txn = 0;
    std::string table;               // for data + DDL ops
    std::vector<uint8_t> before;     // before-image (UPDATE/DELETE)
    std::vector<uint8_t> after;      // after-image  (INSERT/UPDATE)
    std::vector<uint8_t> ddl;        // encoded schema (CREATE_TABLE)
};

class WAL {
public:
    explicit WAL(const std::string& path);
    ~WAL();

    void log_begin(TxnId txn);
    void log_commit(TxnId txn);   // forces the log to disk (durability point)
    void log_abort(TxnId txn);
    void log_insert(TxnId txn, const std::string& table, const std::vector<uint8_t>& after);
    void log_update(TxnId txn, const std::string& table,
                    const std::vector<uint8_t>& before, const std::vector<uint8_t>& after);
    void log_delete(TxnId txn, const std::string& table, const std::vector<uint8_t>& before);
    void log_create_table(const std::string& table, const std::vector<uint8_t>& ddl);

    void flush();                 // fsync the log
    std::vector<LogRecord> read_all();   // parse the whole log for recovery
    void truncate();              // drop the log (used by checkpoint/clean shutdown)

    const std::string& path() const { return path_; }

private:
    void append(const LogRecord& rec, bool force);

    std::string  path_;
    std::ofstream out_;
};

}  // namespace minidb
