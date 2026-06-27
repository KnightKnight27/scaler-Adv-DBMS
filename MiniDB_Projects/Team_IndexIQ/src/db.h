#pragma once
#include "catalog/catalog.h"
#include "storage/heap_file.h"
#include "storage/buffer_pool.h"
#include "index/btree.h"
#include "recovery/wal.h"
#include "transaction/txn_manager.h"
#include "optimizer/optimizer.h"
#include "executor/executor.h"
#include "parser/ast.h"
#include <string>
#include <unordered_map>
#include <vector>

class Database {
public:
    explicit Database(const std::string& data_dir);
    ~Database();

    std::vector<Row> execute(const std::string& sql);

    TxID current_txn() const { return current_txn_; }

private:
    std::string   data_dir_;
    Catalog       catalog_;
    BufferPool    pool_;
    WAL           wal_;
    TransactionManager txn_mgr_;
    Optimizer     optimizer_;
    Executor      executor_;

    std::unordered_map<std::string, HeapFile*> heap_files_;
    std::unordered_map<std::string, BTree*>    indexes_;

    TxID current_txn_ = 0;

    void open_table(const std::string& table_name);
    void rebuild_index(const std::string& table_name);
    void recover();

    std::vector<Row> handle_create(const CreateStmt& s);
    std::vector<Row> handle_insert(const InsertStmt& s);
    std::vector<Row> handle_delete(const DeleteStmt& s);
    std::vector<Row> handle_select(const SelectStmt& s);

    TxID  auto_begin();
    void  auto_commit(TxID xid);
};
