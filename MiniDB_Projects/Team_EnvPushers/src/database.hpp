// Database: the facade that wires every layer together.
//
//   parser -> optimizer -> Volcano executor          (queries)
//   catalog + heap files + buffer pool + disk         (storage)
//   B+ tree primary-key index                         (access path)
//   lock manager + transactions                       (concurrency, strict 2PL)
//   write-ahead log + recovery                        (durability/atomicity)
//
// A statement runs either inside an explicit transaction (BEGIN..COMMIT) or, by
// default, in its own auto-commit transaction. All mutations take table locks,
// append to the WAL before the data pages are flushed, and register in-memory
// undo actions for ROLLBACK.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "catalog/catalog.hpp"
#include "execution/executor.hpp"
#include "optimizer/optimizer.hpp"
#include "recovery/wal.hpp"
#include "sql/ast.hpp"
#include "storage/buffer_pool.hpp"
#include "storage/disk_manager.hpp"
#include "storage/heap_file.hpp"
#include "transaction/transaction.hpp"

namespace minidb {

struct ResultSet {
    bool is_select = false;
    std::vector<std::string> columns;
    std::vector<Tuple> rows;
    std::string message;     // status text for DML/DDL, or error
    bool ok = true;
    std::string explain;     // populated for SELECT (query plan)
};

class Database {
public:
    explicit Database(const std::string& dir, size_t buffer_pool_pages = 256);
    ~Database();

    // Run a single SQL statement in its own auto-commit transaction.
    ResultSet execute(const std::string& sql);
    // Run a statement inside an existing transaction.
    ResultSet execute(const std::string& sql, Transaction* txn);

    Transaction* begin();
    void commit(Transaction* txn);
    void abort(Transaction* txn);

    TransactionManager& txns() { return txn_mgr_; }
    BufferPool& buffer_pool() { return bp_; }
    Catalog& catalog() { return catalog_; }

    // Flush dirty pages + catalog, then truncate the WAL (a checkpoint). Called
    // by the destructor for a clean shutdown so recovery has nothing to do.
    void checkpoint();

private:
    // statement handlers
    ResultSet exec_create(CreateTableStmt* s);
    ResultSet exec_insert(InsertStmt* s, Transaction* txn);
    ResultSet exec_select(SelectStmt* s, Transaction* txn);
    ResultSet exec_delete(DeleteStmt* s, Transaction* txn);
    ResultSet exec_update(UpdateStmt* s, Transaction* txn);

    // storage helpers (also used by recovery)
    TableInfo* require_table(const std::string& name);
    HeapFile* heap_for(const std::string& name);
    void create_table_internal(const std::string& name, const Schema& schema,
                               PageId first_page_id, bool log_and_persist);
    void upsert(TableInfo* t, const Tuple& tuple);          // idempotent (recovery)
    void delete_by_key(TableInfo* t, const Value& key);     // idempotent (recovery)
    void rebuild_indexes();
    void recover();

    static std::vector<uint8_t> encode_schema(const std::string& name, const Schema& s);
    static void decode_schema(const std::vector<uint8_t>& blob, std::string& name, Schema& s);
    const Value& key_of(TableInfo* t, const Tuple& tuple);

    std::string dir_;
    std::unique_ptr<DiskManager> disk_;
    BufferPool bp_;
    Catalog catalog_;
    std::unordered_map<std::string, std::unique_ptr<HeapFile>> heaps_;
    TransactionManager txn_mgr_;
    std::unique_ptr<WAL> wal_;
    Optimizer optimizer_;
    std::string catalog_path_;
};

}  // namespace minidb
