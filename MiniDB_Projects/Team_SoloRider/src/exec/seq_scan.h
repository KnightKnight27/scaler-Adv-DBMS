#pragma once
#include "exec/operator.h"
#include "storage/heap_file.h"

namespace minidb {

class SeqScan : public Operator {
public:
    SeqScan(HeapFile* heap_file, const Schema& schema);
    ~SeqScan() override = default;

    void Open() override;
    bool Next(Tuple& out) override;
    void Close() override;
    const Schema& get_schema() const override { return schema_; }

private:
    HeapFile* heap_file_;
    Schema schema_;
    page_id_t current_page_;
    slot_id_t current_slot_;
};

} // namespace minidb
