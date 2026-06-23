#pragma once

#include "execution/abstract_executor.h"
#include <memory>

namespace minidb {

class DeleteExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child_executor_;
    table_oid_t table_oid_;
    bool has_run_{false};

public:
    DeleteExecutor(ExecutorContext* context, std::unique_ptr<AbstractExecutor> child, table_oid_t table_oid)
        : AbstractExecutor(context), child_executor_(std::move(child)), table_oid_(table_oid) {}

    void Init() override;
    bool Next(Row* row) override;
    const Schema* GetOutputSchema() const override { return nullptr; } 
};

} // namespace minidb