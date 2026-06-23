#pragma once

#include <memory>
#include <string>
#include <vector>

#include "catalog/catalog.h"
#include "common/types.h"
#include "optimizer/optimizer.h"
#include "transaction/transaction_manager.h"

namespace minidb {

class Executor {
public:
    Executor(Catalog* catalog, TransactionManager* txn_manager, bool use_batch = false);

    std::vector<std::vector<std::string>> ExecuteSelect(const std::shared_ptr<PlanNode>& plan);
    int ExecuteInsert(const InsertStmt& stmt);
    int ExecuteDelete(const DeleteStmt& stmt);
    void UndoInsert(const std::string& table, const Row& row, const Rid& rid);
    void UndoDelete(const std::string& table, const Row& row);
    void set_use_batch(bool use_batch) { use_batch_ = use_batch; }

private:
    Catalog* catalog_;
    TransactionManager* txn_manager_;
    bool use_batch_;

    void UpdateIndexes(const TableSchema& schema, const std::string& table, const Row& row,
                       const Rid& rid, bool insert);
    void RemoveFromIndexes(const TableSchema& schema, const std::string& table, const Row& row);

    bool RowMatches(const TableSchema& schema, const Row& row, const std::vector<Predicate>& preds);
    std::vector<std::string> ProjectRow(const TableSchema& schema, const Row& row,
                                        const std::vector<std::string>& cols);
    std::vector<Row> CollectScanRows(const std::shared_ptr<PlanNode>& plan);
    std::vector<std::vector<std::string>> ExecuteAggregate(const std::shared_ptr<PlanNode>& plan,
                                                           const std::vector<Row>& rows);
};

}  // namespace minidb
