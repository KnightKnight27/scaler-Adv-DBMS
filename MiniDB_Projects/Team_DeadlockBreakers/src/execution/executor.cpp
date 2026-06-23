#include "execution/executor.h"
#include "execution/parser.h"
#include "optimizer/optimizer.h"
#include "recovery/wal.h"
#include "transaction/txn_manager.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace minidb {

Executor::Executor(Catalog* catalog, Optimizer* optimizer,
                   TransactionManager* txn_manager)
    : catalog_(catalog), optimizer_(optimizer), txn_manager_(txn_manager) {}

std::vector<Row> Executor::Execute(const std::string& sql, Transaction* txn) {
    auto stmt = Parser::Parse(sql);
    Transaction* active = txn;
    bool auto_commit = !active && txn_manager_;
    if (auto_commit) active = txn_manager_->Begin();

    try {
        std::vector<Row> result;
        switch (stmt->type) {
            case StmtType::SELECT:
                result = ExecuteSelect(static_cast<SelectStmt*>(stmt.get()), active); break;
            case StmtType::INSERT:
                result = ExecuteInsert(static_cast<InsertStmt*>(stmt.get()), active); break;
            case StmtType::DELETE_STMT:
                result = ExecuteDelete(static_cast<DeleteStmt*>(stmt.get()), active); break;
            case StmtType::CREATE_TABLE:
                result = ExecuteCreate(static_cast<CreateTableStmt*>(stmt.get())); break;
        }
        if (auto_commit) {
            bool committed = txn_manager_->Commit(active);
            active = nullptr;
            if (!committed) throw std::runtime_error("Commit failed");
        }
        return result;
    } catch (...) {
        if (auto_commit && active && active->GetState() == TxnState::ACTIVE) {
            txn_manager_->Abort(active);
        }
        throw;
    }
}

static bool Acquire(TransactionManager* manager, Transaction* txn,
                    const std::string& table, LockMode mode) {
    if (!manager || !txn) return true;
    return mode == LockMode::SHARED
        ? manager->GetLockManager()->LockShared(txn, table)
        : manager->GetLockManager()->LockExclusive(txn, table);
}

std::vector<Row> Executor::ExecuteSelect(SelectStmt* stmt, Transaction* txn) {
    std::vector<std::string> lock_order{stmt->table_name};
    if (stmt->has_join) lock_order.push_back(stmt->join_table);
    std::sort(lock_order.begin(), lock_order.end());
    for (const auto& table : lock_order) {
        if (!Acquire(txn_manager_, txn, table, LockMode::SHARED)) {
            throw std::runtime_error("Lock timeout on table: " + table);
        }
    }

    std::vector<Row> results;
    std::unique_ptr<Operator> top;

    if (stmt->has_join) {
        auto order = optimizer_->DecideJoinOrder(
            stmt->table_name, stmt->join_table, catalog_);
        TableInfo* left = catalog_->GetTable(order.first);
        TableInfo* right = catalog_->GetTable(order.second);
        if (!left || !right) throw std::runtime_error("Table not found for JOIN");

        JoinCondition condition = stmt->join_cond;
        if (condition.left_table != order.first) {
            std::swap(condition.left_table, condition.right_table);
            std::swap(condition.left_column, condition.right_column);
        }

        Schema left_schema = left->schema;
        Schema right_schema = right->schema;
        for (auto& column : left_schema) column.name = left->name + "." + column.name;
        for (auto& column : right_schema) column.name = right->name + "." + column.name;

        top = std::make_unique<NestedLoopJoinOperator>(
            std::make_unique<SeqScanOperator>(left),
            std::make_unique<SeqScanOperator>(right), condition,
            left_schema, right_schema);
        if (!stmt->where_clause.IsEmpty()) {
            Schema schema = top->GetOutputSchema();
            top = std::make_unique<FilterOperator>(
                std::move(top), stmt->where_clause, schema);
        }
    } else {
        TableInfo* table = catalog_->GetTable(stmt->table_name);
        if (!table) throw std::runtime_error("Table not found: " + stmt->table_name);

        ScanDecision scan = optimizer_->DecideScan(stmt, catalog_);
        if (scan.scan_type == ScanType::INDEX_SCAN) {
            if (scan.is_range) {
                top = std::make_unique<IndexScanOperator>(
                    table, scan.index_key_lo, scan.index_key_hi);
            } else {
                top = std::make_unique<IndexScanOperator>(
                    table, scan.index_key_lo);
            }
            std::cout << "  [Optimizer] INDEX SCAN on '" << table->name << "'\n";
        } else {
            top = std::make_unique<SeqScanOperator>(table);
            std::cout << "  [Optimizer] SEQ SCAN on '" << table->name << "'\n";
        }

        if (!stmt->where_clause.IsEmpty()) {
            top = std::make_unique<FilterOperator>(
                std::move(top), stmt->where_clause, table->schema);
        }
    }

    Schema input = top->GetOutputSchema();
    auto projection = std::make_unique<ProjectionOperator>(
        std::move(top), stmt->columns, input);
    last_schema_ = projection->GetOutputSchema();
    projection->Open();
    for (;;) {
        TupleBatch batch = projection->Next();
        if (batch.IsEmpty()) break;
        for (auto& row : batch.rows) results.push_back(std::move(row));
    }
    projection->Close();
    return results;
}

std::vector<Row> Executor::ExecuteInsert(InsertStmt* stmt, Transaction* txn) {
    TableInfo* table = catalog_->GetTable(stmt->table_name);
    if (!table) throw std::runtime_error("Table not found: " + stmt->table_name);
    if (stmt->values.size() != table->schema.size()) {
        throw std::runtime_error("Value count does not match schema");
    }
    if (!std::holds_alternative<int64_t>(stmt->values[table->pk_col])) {
        throw std::runtime_error("Primary key must be INT");
    }
    if (!Acquire(txn_manager_, txn, table->name, LockMode::EXCLUSIVE)) {
        throw std::runtime_error("Lock timeout on table: " + table->name);
    }

    int64_t key = std::get<int64_t>(stmt->values[table->pk_col]);
    if (table->primary_index->Search(key) ||
        (txn && !txn->ReservePrimaryKey(table->name, key))) {
        throw std::runtime_error("Duplicate primary key: " + std::to_string(key));
    }

    if (!txn) {
        RecordId record = table->heap_file->InsertTuple(stmt->values);
        if (record.page_id == INVALID_PAGE_ID)
            throw std::runtime_error("Insert failed - no space");
        table->primary_index->Insert(key, record);
        return {};
    }

    txn_manager_->GetWALManager()->AppendLog(txn->GetTxnId(), LogType::INSERT,
        table->name, HeapFile::SerializeRow(stmt->values, table->schema),
        std::to_string(key));
    txn->StageInsert(table, stmt->values, key);
    return {};
}

std::vector<Row> Executor::ExecuteDelete(DeleteStmt* stmt, Transaction* txn) {
    TableInfo* table = catalog_->GetTable(stmt->table_name);
    if (!table) throw std::runtime_error("Table not found: " + stmt->table_name);
    if (!Acquire(txn_manager_, txn, table->name, LockMode::EXCLUSIVE)) {
        throw std::runtime_error("Lock timeout on table: " + table->name);
    }

    std::vector<std::pair<RecordId, Row>> rows;
    table->heap_file->ScanAll([&](const RecordId& rid, const Row& row) {
        if (FilterOperator::EvalWhereClause(stmt->where_clause, row, table->schema)) {
            rows.push_back({rid, row});
        }
    });

    if (!txn) {
        for (const auto& item : rows) {
            if (!table->heap_file->DeleteTuple(item.first))
                throw std::runtime_error("Delete failed");
            int64_t key = std::get<int64_t>(item.second[table->pk_col]);
            table->primary_index->Delete(key);
        }
        return {};
    }

    for (const auto& item : rows) {
        int64_t key = std::get<int64_t>(item.second[table->pk_col]);
        txn_manager_->GetWALManager()->AppendLog(txn->GetTxnId(),
            LogType::DELETE_LOG, table->name,
            HeapFile::SerializeRow(item.second, table->schema),
            std::to_string(key));
        txn->StageDelete(table, item.first, key);
    }
    return {};
}

std::vector<Row> Executor::ExecuteCreate(CreateTableStmt* stmt) {
    if (catalog_->GetTable(stmt->table_name)) {
        throw std::runtime_error("Table already exists: " + stmt->table_name);
    }
    auto storage = std::make_unique<TableStorage>();
    storage->disk = std::make_unique<DiskManager>(stmt->table_name + ".db");
    storage->bpm = std::make_unique<BufferPoolManager>(
        BUFFER_POOL_SIZE, storage->disk.get());
    if (!catalog_->CreateTable(stmt->table_name, stmt->columns,
                               storage->bpm.get())) {
        throw std::runtime_error("Failed to create table");
    }
    storage_[stmt->table_name] = std::move(storage);
    return {};
}

} // namespace minidb
