#include "execution/operators.h"

namespace minidb {

NestedLoopJoinOperator::NestedLoopJoinOperator(
    std::unique_ptr<Operator> left, std::unique_ptr<Operator> right,
    const JoinCondition& condition, const Schema& left_schema,
    const Schema& right_schema)
    : left_(std::move(left)), right_(std::move(right)), cond_(condition),
      left_schema_(left_schema), right_schema_(right_schema) {
    output_schema_ = left_schema_;
    output_schema_.insert(output_schema_.end(), right_schema_.begin(), right_schema_.end());
}

int NestedLoopJoinOperator::FindCol(const Schema& schema,
                                    const std::string& column) const {
    bool qualified = column.find('.') != std::string::npos;
    for (size_t index = 0; index < schema.size(); ++index) {
        std::string name = schema[index].name;
        if (qualified && name == column) return static_cast<int>(index);
        if (qualified) continue;
        size_t dot = name.find('.');
        if (dot != std::string::npos) name = name.substr(dot + 1);
        if (name == column) return static_cast<int>(index);
    }
    return -1;
}

void NestedLoopJoinOperator::Open() {
    left_->Open();
    right_->Open();
    right_rows_.clear();
    for (;;) {
        TupleBatch batch = right_->Next();
        if (batch.IsEmpty()) break;
        for (auto& row : batch.rows) right_rows_.push_back(std::move(row));
    }
    left_batch_ = left_->Next();
    left_idx_ = right_idx_ = 0;
    left_exhausted_ = left_batch_.IsEmpty();
}

TupleBatch NestedLoopJoinOperator::Next() {
    TupleBatch output;
    int left_col = FindCol(left_schema_, cond_.left_column);
    int right_col = FindCol(right_schema_, cond_.right_column);
    while (!output.IsFull() && !left_exhausted_) {
        if (left_idx_ == left_batch_.Size()) {
            left_batch_ = left_->Next();
            left_idx_ = right_idx_ = 0;
            if (left_batch_.IsEmpty()) { left_exhausted_ = true; break; }
        }
        const Row& left_row = left_batch_.rows[left_idx_];
        while (right_idx_ < right_rows_.size() && !output.IsFull()) {
            const Row& right_row = right_rows_[right_idx_++];
            if (left_col >= 0 && right_col >= 0 &&
                CompareValues(left_row[left_col], right_row[right_col], CmpOp::EQ)) {
                Row joined = left_row;
                joined.insert(joined.end(), right_row.begin(), right_row.end());
                output.AddRow(std::move(joined));
            }
        }
        if (right_idx_ == right_rows_.size()) { left_idx_++; right_idx_ = 0; }
    }
    return output;
}

void NestedLoopJoinOperator::Close() {
    left_->Close(); right_->Close(); right_rows_.clear();
}
Schema NestedLoopJoinOperator::GetOutputSchema() const { return output_schema_; }

} // namespace minidb
