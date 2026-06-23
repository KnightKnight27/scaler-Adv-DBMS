#include "executor/executor.h"

#include <unordered_map>

#include "executor/execution_metrics.h"
#include "executor/vectorized.h"

namespace minidb {

Executor::Executor(Catalog* catalog, TransactionManager* txn_manager, bool use_batch)
    : catalog_(catalog), txn_manager_(txn_manager), use_batch_(use_batch) {}

void Executor::UpdateIndexes(const TableSchema& schema, const std::string& table, const Row& row,
                             const Rid& rid, bool insert) {
    if (auto* pk = schema.PrimaryKeyColumn()) {
        int pk_idx = schema.ColumnIndex(pk->name);
        if (auto* index = catalog_->GetPrimaryIndex(table)) {
            if (insert) {
                index->Insert(row.Get(static_cast<std::size_t>(pk_idx)), rid);
            } else {
                index->Remove(row.Get(static_cast<std::size_t>(pk_idx)));
            }
        }
    }
    for (const auto& col : schema.columns) {
        if (col.indexed && !col.primary_key) {
            if (auto* sec = catalog_->GetSecondaryIndex(table, col.name)) {
                int idx = schema.ColumnIndex(col.name);
                if (insert) {
                    sec->Insert(row.Get(static_cast<std::size_t>(idx)), rid);
                } else {
                    sec->Remove(row.Get(static_cast<std::size_t>(idx)));
                }
            }
        }
    }
}

void Executor::RemoveFromIndexes(const TableSchema& schema, const std::string& table,
                                 const Row& row) {
    UpdateIndexes(schema, table, row, Rid{}, false);
}

bool Executor::RowMatches(const TableSchema& schema, const Row& row,
                          const std::vector<Predicate>& preds) {
    for (const auto& pred : preds) {
        int idx = schema.ColumnIndex(pred.column);
        if (idx >= static_cast<int>(row.values.size())) {
            return false;
        }
        if (!CompareValues(row.Get(static_cast<std::size_t>(idx)), pred.op, pred.value)) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> Executor::ProjectRow(const TableSchema& schema, const Row& row,
                                              const std::vector<std::string>& cols) {
    if (cols.empty()) {
        std::vector<std::string> all;
        for (const auto& v : row.values) all.push_back(v.ToString());
        return all;
    }
    std::vector<std::string> projected;
    for (const auto& col : cols) {
        projected.push_back(row.Get(static_cast<std::size_t>(schema.ColumnIndex(col))).ToString());
    }
    return projected;
}

std::vector<Row> Executor::CollectScanRows(const std::shared_ptr<PlanNode>& plan) {
    std::vector<Row> rows;
    auto schema = catalog_->GetTable(plan->table);
    if (!schema) return rows;
    auto* heap = catalog_->GetHeapFile(plan->table);
    txn_manager_->LockTable(plan->table, LockMode::SHARED);

    if (plan->type == PlanType::INDEX_SCAN && !plan->predicates.empty() &&
        plan->predicates.front().op == CompareOp::EQ) {
        const auto& pred = plan->predicates.front();
        BPlusTree* index = catalog_->GetPrimaryIndex(plan->table);
        const auto* pk_col = schema->PrimaryKeyColumn();
        if (!pk_col || pred.column != pk_col->name) {
            index = catalog_->GetSecondaryIndex(plan->table, pred.column);
        }
        if (index) {
            if (auto rid = index->Search(pred.value)) {
                if (auto row = heap->GetTuple(*rid)) {
                    if (RowMatches(*schema, *row, plan->predicates)) rows.push_back(*row);
                }
            }
            return rows;
        }
    }

    if (use_batch_) {
        return VectorizedExecutor::BatchScan(heap, *schema, plan->predicates);
    }
    ExecutionMetricsHolder::Reset();
    auto& metrics = ExecutionMetricsHolder::Get();
    for (const Rid& rid : heap->ScanAll()) {
        if (auto row = heap->GetTuple(rid)) {
            metrics.tuples_scanned++;
            if (RowMatches(*schema, *row, plan->predicates)) {
                metrics.tuples_output++;
                rows.push_back(*row);
            }
        }
    }
    return rows;
}

std::vector<std::vector<std::string>> Executor::ExecuteAggregate(
    const std::shared_ptr<PlanNode>& plan, const std::vector<Row>& rows) {
    auto schema = catalog_->GetTable(plan->table);
    if (!schema) return {};

    struct GroupKey {
        std::vector<std::string> parts;
        bool operator==(const GroupKey& other) const { return parts == other.parts; }
    };

    struct GroupKeyHash {
        std::size_t operator()(const GroupKey& key) const {
            std::size_t h = 0;
            for (const auto& part : key.parts) {
                h ^= std::hash<std::string>{}(part) + 0x9e3779b9 + (h << 6) + (h >> 2);
            }
            return h;
        }
    };

    std::unordered_map<GroupKey, std::vector<Row>, GroupKeyHash> groups;
    if (plan->group_by.empty()) {
        groups[GroupKey{}] = rows;
    } else {
        for (const auto& row : rows) {
            GroupKey key;
            for (const auto& col : plan->group_by) {
                key.parts.push_back(row.Get(static_cast<std::size_t>(schema->ColumnIndex(col)))
                                        .ToString());
            }
            groups[key].push_back(row);
        }
    }

    std::vector<std::vector<std::string>> result;
    for (const auto& [key, group_rows] : groups) {
        std::vector<std::string> out_row;
        out_row.insert(out_row.end(), key.parts.begin(), key.parts.end());
        for (const auto& agg : plan->aggregates) {
            if (agg.func == AggFunc::COUNT) {
                if (agg.column.empty()) {
                    out_row.push_back(std::to_string(group_rows.size()));
                } else {
                    int col_idx = schema->ColumnIndex(agg.column);
                    int count = 0;
                    for (const auto& row : group_rows) {
                        const Value& v = row.Get(static_cast<std::size_t>(col_idx));
                        if (v.type != ValueType::NULL_TYPE) ++count;
                    }
                    out_row.push_back(std::to_string(count));
                }
            }
        }
        result.push_back(std::move(out_row));
    }
    return result;
}

std::vector<std::vector<std::string>> Executor::ExecuteSelect(
    const std::shared_ptr<PlanNode>& plan) {
    std::vector<std::vector<std::string>> result;

    if (plan->type == PlanType::AGGREGATE) {
        return ExecuteAggregate(plan, CollectScanRows(plan->left));
    }

    if (plan->type == PlanType::NESTED_LOOP_JOIN) {
        auto left_schema = catalog_->GetTable(plan->join.left_table);
        auto right_schema = catalog_->GetTable(plan->join.right_table);
        if (!left_schema || !right_schema) return result;

        auto* left_heap = catalog_->GetHeapFile(plan->join.left_table);
        auto* right_heap = catalog_->GetHeapFile(plan->join.right_table);
        std::vector<Row> left_rows;
        std::vector<Row> right_rows;

        if (use_batch_) {
            left_rows = VectorizedExecutor::BatchScan(left_heap, *left_schema, plan->left->predicates);
            right_rows =
                VectorizedExecutor::BatchScan(right_heap, *right_schema, plan->right->predicates);
        } else {
            for (const Rid& rid : left_heap->ScanAll()) {
                if (auto row = left_heap->GetTuple(rid)) {
                    if (RowMatches(*left_schema, *row, plan->left->predicates))
                        left_rows.push_back(*row);
                }
            }
            for (const Rid& rid : right_heap->ScanAll()) {
                if (auto row = right_heap->GetTuple(rid)) {
                    if (RowMatches(*right_schema, *row, plan->right->predicates))
                        right_rows.push_back(*row);
                }
            }
        }

        int lidx = left_schema->ColumnIndex(plan->join.left_col);
        int ridx = right_schema->ColumnIndex(plan->join.right_col);
        for (const auto& l : left_rows) {
            for (const auto& r : right_rows) {
                if (l.Get(static_cast<std::size_t>(lidx)) == r.Get(static_cast<std::size_t>(ridx))) {
                    Row joined;
                    joined.values = l.values;
                    joined.values.insert(joined.values.end(), r.values.begin(), r.values.end());
                    if (RowMatches(*left_schema, l, plan->predicates)) {
                        TableSchema combined = *left_schema;
                        combined.columns.insert(combined.columns.end(), right_schema->columns.begin(),
                                                right_schema->columns.end());
                        result.push_back(ProjectRow(combined, joined, plan->project_columns));
                    }
                }
            }
        }
        return result;
    }

    auto schema = catalog_->GetTable(plan->table);
    if (!schema) return result;

    std::vector<Row> rows = CollectScanRows(plan);
    for (const auto& row : rows) {
        result.push_back(ProjectRow(*schema, row, plan->project_columns));
    }
    return result;
}

int Executor::ExecuteInsert(const InsertStmt& stmt) {
    auto schema = catalog_->GetTable(stmt.table);
    if (!schema) throw std::runtime_error("Unknown table");
    txn_manager_->LockTable(stmt.table, LockMode::EXCLUSIVE);

    Row row;
    if (stmt.columns.empty()) {
        row.values = stmt.values;
    } else {
        row.values.assign(schema->columns.size(), Value::Null());
        for (std::size_t i = 0; i < stmt.columns.size(); ++i) {
            int idx = schema->ColumnIndex(stmt.columns[i]);
            row.values[static_cast<std::size_t>(idx)] = stmt.values[i];
        }
    }

    auto* heap = catalog_->GetHeapFile(stmt.table);
    Rid rid = heap->InsertTuple(row);
    UpdateIndexes(*schema, stmt.table, row, rid, true);
    txn_manager_->LogInsert(stmt.table, row, rid);
    return 1;
}

int Executor::ExecuteDelete(const DeleteStmt& stmt) {
    auto schema = catalog_->GetTable(stmt.table);
    if (!schema) throw std::runtime_error("Unknown table");
    txn_manager_->LockTable(stmt.table, LockMode::EXCLUSIVE);
    auto* heap = catalog_->GetHeapFile(stmt.table);
    int count = 0;
    for (const Rid& rid : heap->ScanAll()) {
        if (auto row = heap->GetTuple(rid)) {
            if (RowMatches(*schema, *row, stmt.predicates)) {
                txn_manager_->LogDelete(stmt.table, *row, rid);
                RemoveFromIndexes(*schema, stmt.table, *row);
                heap->DeleteTuple(rid);
                count++;
            }
        }
    }
    return count;
}

void Executor::UndoInsert(const std::string& table, const Row& row, const Rid& rid) {
    auto schema = catalog_->GetTable(table);
    if (!schema) return;
    auto* heap = catalog_->GetHeapFile(table);
    if (!heap) return;
    RemoveFromIndexes(*schema, table, row);
    heap->DeleteTuple(rid);
}

void Executor::UndoDelete(const std::string& table, const Row& row) {
    auto schema = catalog_->GetTable(table);
    if (!schema) return;
    auto* heap = catalog_->GetHeapFile(table);
    if (!heap) return;
    Rid rid = heap->InsertTuple(row);
    UpdateIndexes(*schema, table, row, rid, true);
}

}  // namespace minidb
