#include "benchmark_sources.h"

namespace minidb::bench {

BatchSource::BatchSource(std::vector<Row> rows, Schema schema)
    : schema_(std::move(schema)) {
    TupleBatch batch;
    for (auto& row : rows) {
        batch.AddRow(std::move(row));
        if (batch.IsFull()) {
            batches_.push_back(std::move(batch));
            batch = TupleBatch{};
        }
    }
    if (!batch.IsEmpty()) batches_.push_back(std::move(batch));
}

void BatchSource::Open() { cursor_ = 0; }

TupleBatch BatchSource::Next() {
    if (cursor_ == batches_.size()) return {};
    return std::move(batches_[cursor_++]);
}

RowSource::RowSource(std::vector<Row> rows, Schema schema)
    : rows_(std::move(rows)), schema_(std::move(schema)) {}

void RowSource::Open() { cursor_ = 0; }

bool RowSource::Next(Row& row) {
    if (cursor_ == rows_.size()) return false;
    row = std::move(rows_[cursor_++]);
    return true;
}

} // namespace minidb::bench
