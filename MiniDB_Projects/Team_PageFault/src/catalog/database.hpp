#pragma once
#include <string>
#include <vector>
#include <memory>
#include "schema.hpp"
#include "catalog.hpp"
#include "../common/value.hpp"
#include "../storage/disk_manager.hpp"
#include "../storage/buffer_pool.hpp"
#include "../recovery/wal.hpp"

namespace minidb {

// A single-connection database session. It owns the storage stack (disk, buffer
// pool, catalog) and the write-ahead log, and provides the transactional write
// path (INSERT/DELETE wrapped in BEGIN/COMMIT/ROLLBACK or auto-commit). Reads
// are served by the executor directly off each table's heap and index.
class Database {
public:
    explicit Database(const std::string& db_path);
    ~Database();

    // DDL — persisted immediately via the catalog (no WAL needed).
    Table* createTable(const std::string& name, const Schema& schema);
    Table* getTable(const std::string& name) const { return catalog_.get(name); }
    const Catalog& catalog() const { return catalog_; }

    // DML — transactional. Throws on a duplicate primary key.
    void insertRow(Table* t, const std::vector<Value>& row);
    // Delete the row with this primary key; returns true if a row was removed.
    bool deleteByKey(Table* t, int64_t pk);

    // Transaction control.
    void begin();
    void commit();
    void rollback();
    bool inTransaction() const { return current_txn_ != INVALID_TXN; }

    // Flush dirty pages so a clean restart sees all committed data.
    void flush();

private:
    std::string db_path_;
    std::unique_ptr<DiskManager> disk_;
    std::unique_ptr<BufferPool> bp_;
    Catalog catalog_;
    WAL wal_;

    TxnId current_txn_ = INVALID_TXN;
    TxnId next_txn_ = 1;
    bool autocommit_started_ = false; // did the current op open an implicit txn?

    // Inverse operations to replay on ROLLBACK, in insertion order.
    struct Undo {
        bool was_insert;       // true: undo by deleting; false: undo by inserting
        std::string table;
        int64_t pk;
        std::string image;     // before-image (for re-insert on undo of a delete)
    };
    std::vector<Undo> undo_;

    // Open an implicit single-statement transaction if none is active.
    bool ensureTxn();
    void maybeAutoCommit(bool implicit);

    // Crash recovery: REDO committed ops, UNDO loser ops (idempotent by pk).
    void recover();
    // Apply an insert/delete to heap+index without logging (used by recovery
    // and rollback).
    void physInsert(Table* t, int64_t pk, const std::string& image);
    void physDelete(Table* t, int64_t pk);
};

} // namespace minidb
