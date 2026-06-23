#pragma once
#include "exec/operator.h"
#include "sql/ast.h"

namespace minidb {

class Filter : public Operator {
public:
    Filter(std::unique_ptr<Operator> child, ASTNode* predicate);
    ~Filter() override = default;

    void Open() override;
    bool Next(Tuple& out) override;
    void Close() override;
    const Schema& get_schema() const override { return child_->get_schema(); }

private:
    std::unique_ptr<Operator> child_;
    ASTNode* predicate_;

    bool evaluate(const Tuple& tuple, ASTNode* expr);
};

} // namespace minidb
