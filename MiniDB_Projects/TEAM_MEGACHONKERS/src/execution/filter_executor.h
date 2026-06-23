#pragma once

#include "execution/abstract_executor.h"
#include <memory>
#include <string>

namespace minidb {

class FilterExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child_executor_;
    uint32_t filter_col_idx_;
    std::string match_value_;
    const Schema* output_schema_;

public:
    FilterExecutor(ExecutorContext* context, 
                   std::unique_ptr<AbstractExecutor> child, 
                   uint32_t filter_col_idx, 
                   std::string match_value)
        : AbstractExecutor(context), 
          child_executor_(std::move(child)), 
          filter_col_idx_(filter_col_idx), 
          match_value_(std::move(match_value)) {}

    void Init() override {
        child_executor_->Init();
        output_schema_ = child_executor_->GetOutputSchema();
    }

    bool Next(Row* row) override {
        // Keep pulling from the child until we find a match or hit EOF
        while (child_executor_->Next(row)) {
            if (row->columns[filter_col_idx_] == match_value_) {
                return true; // Match found! Pass it up.
            }
        }
        return false; // EOF
    }

    const Schema* GetOutputSchema() const override { return output_schema_; }
};

} // namespace minidb