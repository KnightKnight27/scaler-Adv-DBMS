#pragma once

#include "execution/abstract_executor.h"
#include "parser/expression.h"
#include <memory>
#include <string>

namespace minidb {

// Filters rows coming from a child operator. Supports two modes:
//
//   1. Legacy single-column equality: FilterExecutor(ctx, child, col_idx, value)
//      -- kept for the original optimizer/tests which build a bare predicate.
//   2. Rich expression predicate: FilterExecutor(ctx, child, predicate)
//      -- evaluates an arbitrary boolean Expression tree (AND/OR/comparisons)
//         against each row. This is what the planner emits for real WHERE
//         clauses.
class FilterExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child_executor_;

    // Legacy mode state (used only when predicate_ is null).
    uint32_t filter_col_idx_{0};
    std::string match_value_;

    // Expression mode state.
    ExprPtr predicate_;

    const Schema* output_schema_{nullptr};

public:
    // Legacy single-column equality filter.
    FilterExecutor(ExecutorContext* context,
                   std::unique_ptr<AbstractExecutor> child,
                   uint32_t filter_col_idx,
                   std::string match_value)
        : AbstractExecutor(context),
          child_executor_(std::move(child)),
          filter_col_idx_(filter_col_idx),
          match_value_(std::move(match_value)) {}

    // Rich expression-tree filter.
    FilterExecutor(ExecutorContext* context,
                   std::unique_ptr<AbstractExecutor> child,
                   ExprPtr predicate)
        : AbstractExecutor(context),
          child_executor_(std::move(child)),
          predicate_(std::move(predicate)) {}

    void Init() override {
        child_executor_->Init();
        output_schema_ = child_executor_->GetOutputSchema();
    }

    bool Next(Row* row) override {
        // Keep pulling from the child until a row satisfies the predicate.
        while (child_executor_->Next(row)) {
            if (predicate_) {
                if (predicate_->EvalBool(*row, output_schema_)) return true;
            } else {
                if (filter_col_idx_ < row->columns.size() &&
                    row->columns[filter_col_idx_] == match_value_) {
                    return true;
                }
            }
        }
        return false; // EOF
    }

    const Schema* GetOutputSchema() const override { return output_schema_; }
};

} // namespace minidb
