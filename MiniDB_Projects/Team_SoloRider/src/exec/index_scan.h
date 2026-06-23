#pragma once
#include "exec/operator.h"
#include "index/bplus_tree.h"
#include "storage/heap_file.h"

namespace minidb {

class IndexScan : public Operator {
public:
    IndexScan(BPlusTree* tree, HeapFile* heap_file, const Schema& schema, int search_key);
    ~IndexScan() override = default;

    void Open() override;
    bool Next(Tuple& out) override;
    void Close() override;
    const Schema& get_schema() const override { return schema_; }

private:
    BPlusTree* tree_;
    HeapFile* heap_file_;
    Schema schema_;
    int search_key_;
    bool done_;
};

} // namespace minidb
