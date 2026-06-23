#pragma once

#include "execution/catalog.h"
#include "execution/plan.h"
#include "parser/ast.h"

namespace minidb {

class Optimizer {
public:
    explicit Optimizer(Catalog* catalog);

    std::unique_ptr<PlanNode> Optimize(const Statement& statement);

private:
    std::unique_ptr<PlanNode> OptimizeSelect(const SelectStatement& select);
    std::unique_ptr<PlanNode> OptimizeScanWithFilter(const std::string& table,
                                                    std::unique_ptr<Expr> predicate);
    std::unique_ptr<PlanNode> BuildAccessPath(const std::string& table,
                                              const BinaryExpr& predicate);
    bool TryExtractPredicate(const Expr* expr, std::string* column, std::string* op,
                             int64_t* bound) const;
    double EstimateCost(PlanType type, const std::string& table,
                        const std::string& column, const std::string& op,
                        int64_t bound) const;

    Catalog* catalog_;
};

}  // namespace minidb
