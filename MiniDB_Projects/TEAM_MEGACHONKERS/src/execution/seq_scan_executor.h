#pragma once

#include "execution/abstract_executor.h"
#include <map>
#include <string>

namespace minidb {

class SeqScanExecutor : public AbstractExecutor {
private:
    table_oid_t table_oid_;
    
    // We merge the SSTables and MemTable into this map during Init()
    std::map<std::string, std::string> merged_view_;
    std::map<std::string, std::string>::iterator cursor_;

public:
    SeqScanExecutor(ExecutorContext* context, table_oid_t table_oid)
        : AbstractExecutor(context), table_oid_(table_oid) {}

    void Init() override;
    bool Next(Row* row) override;
    
    const Schema* GetOutputSchema() const override {
        return context_->GetCatalog()->GetTable(table_oid_)->schema.get();
    }
};

} // namespace minidb