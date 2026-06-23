#pragma once
#include "exec/operator.h"
#include "sql/ast.h"

namespace minidb {

class NestedLoopJoin : public Operator {
public:
    NestedLoopJoin(std::unique_ptr<Operator> left, std::unique_ptr<Operator> right, ASTNode* condition);
    ~NestedLoopJoin() override = default;

    void Open() override;
    bool Next(Tuple& out) override;
    void Close() override;
    const Schema& get_schema() const override { return output_schema_; }

private:
    std::unique_ptr<Operator> left_;
    std::unique_ptr<Operator> right_;
    ASTNode* condition_;
    Schema output_schema_;
    
    Tuple left_tuple_;
    bool has_left_;

    bool evaluate_join(const Tuple& left_tuple, const Tuple& right_tuple);
};

} // namespace minidb
