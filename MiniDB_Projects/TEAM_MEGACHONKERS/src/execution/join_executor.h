#pragma once

#include "execution/abstract_executor.h"
#include <memory>

namespace minidb {

class NestedLoopJoinExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> left_child_;
    std::unique_ptr<AbstractExecutor> right_child_;
    
    // The indices of the columns to join on (e.g., left.columns[0] == right.columns[1])
    uint32_t left_col_idx_;
    uint32_t right_col_idx_;
    
    // The combined schema of both tables
    std::unique_ptr<Schema> output_schema_;

    // State tracking for the outer loop
    Row current_left_row_;
    bool has_left_row_{false};

public:
    NestedLoopJoinExecutor(ExecutorContext* context,
                           std::unique_ptr<AbstractExecutor> left_child,
                           std::unique_ptr<AbstractExecutor> right_child,
                           uint32_t left_col_idx,
                           uint32_t right_col_idx);

    void Init() override;
    bool Next(Row* row) override;
    const Schema* GetOutputSchema() const override { return output_schema_.get(); }
};

} // namespace minidb