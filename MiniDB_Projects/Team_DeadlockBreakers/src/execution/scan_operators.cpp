#include "execution/operators.h"

namespace minidb {

SeqScanOperator::SeqScanOperator(TableInfo* table) : table_(table) {}

void SeqScanOperator::Open() {
    cursor_ = 0;
    all_rows_.clear();
    table_->heap_file->ScanAll([&](const RecordId&, const Row& row) {
        all_rows_.push_back(row);
    });
}

TupleBatch SeqScanOperator::Next() {
    TupleBatch batch;
    while (cursor_ < all_rows_.size() && !batch.IsFull()) {
        batch.AddRow(std::move(all_rows_[cursor_++]));
    }
    return batch;
}

void SeqScanOperator::Close() { all_rows_.clear(); }
Schema SeqScanOperator::GetOutputSchema() const { return table_->schema; }

IndexScanOperator::IndexScanOperator(TableInfo* table, int64_t key)
    : table_(table), is_range_(false), key_lo_(key), key_hi_(key) {}

IndexScanOperator::IndexScanOperator(TableInfo* table, int64_t lo, int64_t hi)
    : table_(table), is_range_(true), key_lo_(lo), key_hi_(hi) {}

void IndexScanOperator::Open() {
    done_ = false;
    result_rows_.clear();
    if (!table_->primary_index) return;
    std::vector<RecordId> records;
    if (is_range_) records = table_->primary_index->RangeSearch(key_lo_, key_hi_);
    else {
        auto record = table_->primary_index->Search(key_lo_);
        if (record) records.push_back(*record);
    }
    for (const auto& record : records) {
        Row row;
        if (table_->heap_file->GetTuple(record, row)) result_rows_.push_back(std::move(row));
    }
}

TupleBatch IndexScanOperator::Next() {
    TupleBatch batch;
    if (done_) return batch;
    for (auto& row : result_rows_) batch.AddRow(std::move(row));
    done_ = true;
    return batch;
}

void IndexScanOperator::Close() { result_rows_.clear(); }
Schema IndexScanOperator::GetOutputSchema() const { return table_->schema; }

} // namespace minidb
