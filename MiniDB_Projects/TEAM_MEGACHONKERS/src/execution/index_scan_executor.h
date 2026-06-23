#pragma once

#include "execution/abstract_executor.h"
#include "storage/btree/bplus_tree.h"
#include "catalog/catalog.h"
#include <string>
#include <memory>

namespace minidb {

class IndexScanExecutor : public AbstractExecutor {
private:
    BPlusTree* btree_index_;
    std::string search_key_;
    table_oid_t table_oid_;
    bool has_run_{false};
    const Schema* output_schema_;

public:
    IndexScanExecutor(ExecutorContext* context, table_oid_t table_oid, BPlusTree* btree_index, std::string search_key)
        : AbstractExecutor(context), btree_index_(btree_index), search_key_(std::move(search_key)), table_oid_(table_oid) {}

    void Init() override {
        TableMetadata* table = context_->GetCatalog()->GetTable(table_oid_);
        output_schema_ = table->schema.get();
        has_run_ = false;
    }

    bool Next(Row* row) override {
        if (has_run_) return false;

        // Utilize the B+ Tree index for an O(log N) point lookup instead of an O(N) SeqScan
        std::optional<std::string> value = btree_index_->Search(search_key_);
        
        if (value.has_value()) {
            // Reconstruct the Row directly from the index payload
            *row = Row::Deserialize(value.value());
            has_run_ = true;
            return true; // Match found and passed up the pipeline
        }
        
        return false; // EOF or Not Found
    }

    const Schema* GetOutputSchema() const override { return output_schema_; }
};

} // namespace minidb