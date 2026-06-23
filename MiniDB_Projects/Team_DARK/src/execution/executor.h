#pragma once

#include "concurrency/transaction_manager.h"
#include "execution/catalog.h"
#include "execution/plan.h"
#include "parser/ast.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace minidb {

struct QueryResult {
    std::vector<std::string> rows;
    PlanType root_plan_type = PlanType::SEQ_SCAN;
    bool used_index_scan = false;
};

struct ExecutionContext {
    TransactionManager* tx_manager = nullptr;
    Catalog* catalog = nullptr;
    TxID txid = 0;
};

class VolcanoExecutor {
public:
    virtual ~VolcanoExecutor() = default;
    virtual void Init() = 0;
    virtual std::optional<Row> Next() = 0;
    virtual void Close() = 0;
};

class ExecutorFactory {
public:
    static std::unique_ptr<VolcanoExecutor> Create(const PlanNode& plan,
                                                   const ExecutionContext& ctx);
};

class QueryEngine {
public:
    explicit QueryEngine(TransactionManager* tx_manager);

    QueryResult ExecuteSql(const std::string& sql);
    QueryResult ExecutePlanWithTx(const PlanNode& plan, TxID txid);
    std::unique_ptr<PlanNode> OptimizeStatement(const Statement& statement);
    PlanType FindAccessPlanType(const PlanNode& plan) const;
    bool PlanUsesIndexScan(const PlanNode& plan) const;

    Catalog& GetCatalog() { return catalog_; }
    TransactionManager& GetTransactionManager() { return *tx_manager_; }

    void SeedDemoData();

private:
    QueryResult ExecutePlan(const PlanNode& plan, TxID txid);
    std::unique_ptr<VolcanoExecutor> BuildExecutor(const PlanNode& plan,
                                                   const ExecutionContext& ctx);

    TransactionManager* tx_manager_;
    Catalog catalog_;
};

bool EvaluatePredicate(const Expr* expr, const Row& row);
int64_t GetIntColumn(const Row& row, const std::string& column);
std::string GetStringColumn(const Row& row, const std::string& column);

}  // namespace minidb
