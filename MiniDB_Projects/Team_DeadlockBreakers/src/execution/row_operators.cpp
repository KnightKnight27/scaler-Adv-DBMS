#include "execution/operators.h"

namespace minidb {

SeqScanRowOperator::SeqScanRowOperator(TableInfo* table) : table_(table) {}
void SeqScanRowOperator::Open() {
    cursor_ = 0;
    all_rows_.clear();
    table_->heap_file->ScanAll([&](const RecordId&, const Row& row) {
        all_rows_.push_back(row);
    });
}
bool SeqScanRowOperator::Next(Row& row) {
    if (cursor_ == all_rows_.size()) return false;
    row = std::move(all_rows_[cursor_++]);
    return true;
}
void SeqScanRowOperator::Close() { all_rows_.clear(); }
Schema SeqScanRowOperator::GetOutputSchema() const { return table_->schema; }

FilterRowOperator::FilterRowOperator(std::unique_ptr<RowOperator> child,
    const WhereClause& clause, const Schema& schema)
    : child_(std::move(child)), where_clause_(clause), schema_(schema) {}
void FilterRowOperator::Open() { child_->Open(); }
bool FilterRowOperator::Next(Row& row) {
    while (child_->Next(row))
        if (FilterOperator::EvalWhereClause(where_clause_, row, schema_)) return true;
    return false;
}
void FilterRowOperator::Close() { child_->Close(); }
Schema FilterRowOperator::GetOutputSchema() const { return schema_; }

} // namespace minidb
