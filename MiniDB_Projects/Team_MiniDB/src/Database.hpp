#pragma once

#include <string>

using namespace std;

#include "catalog/Catalog.hpp"
#include "common/Config.hpp"
#include "execution/Executor.hpp"
#include "optimizer/Optimizer.hpp"
#include "parser/Parser.hpp"
#include "recovery/RecoveryManager.hpp"
#include "recovery/WAL.hpp"
#include "storage/BufferPool.hpp"
#include "storage/PageManager.hpp"
#include "transaction/TransactionManager.hpp"

namespace minidb {

class Database {
public:
    Database();
    bool open(const string& name);
    void close();
    string executeSQL(const string& sql, int& current_txn);
    void setBatchMode(bool on) { executor_.setBatchMode(on); }

private:
    BufferPool buffer_pool_;
    PageManager page_manager_;
    Catalog catalog_;
    Optimizer optimizer_;
    Executor executor_;
    TransactionManager txn_manager_;
    WAL wal_;
    RecoveryManager recovery_;
    string db_path_;
    bool crashed_ = false;

    string handleCreateTable(const ParsedStatement& stmt);
    string handleInsert(const ParsedStatement& stmt, int txn_id);
    string handleSelect(const ParsedStatement& stmt, int txn_id);
    string handleDelete(const ParsedStatement& stmt, int txn_id);
    string formatRows(const RowList& rows);
    Row mapInsertValues(const ParsedStatement& stmt, const TableDef& def);
};

}  // namespace minidb
