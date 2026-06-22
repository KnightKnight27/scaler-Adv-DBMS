#pragma once

#include "execution/abstract_executor.h"
#include <vector>

namespace minidb {

class ValuesExecutor : public AbstractExecutor {
private:
    std::vector<Row> values_;
    size_t cursor_{0};
    const Schema* dummy_schema_{nullptr}; // Values executor doesn't strictly need a complex schema

public:
    ValuesExecutor(ExecutorContext* context, std::vector<Row> values)
        : AbstractExecutor(context), values_(std::move(values)) {}

    void Init() override {
        cursor_ = 0;
    }

    bool Next(Row* row) override {
        if (cursor_ < values_.size()) {
            *row = values_[cursor_++];
            return true;
        }
        return false;
    }

    const Schema* GetOutputSchema() const override { return dummy_schema_; }
};

} // namespace minidb