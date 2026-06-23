#include "execution/operators.h"

namespace minidb {

ProjectionOperator::ProjectionOperator(std::unique_ptr<Operator> child,
    const std::vector<std::string>& columns, const Schema& input_schema)
    : child_(std::move(child)), columns_(columns), input_schema_(input_schema) {
    if (columns.size() == 1 && columns[0] == "*") {
        output_schema_ = input_schema_;
        for (size_t i = 0; i < input_schema_.size(); ++i)
            col_indices_.push_back(static_cast<int>(i));
        return;
    }

    for (const auto& column : columns) {
        bool qualified = column.find('.') != std::string::npos;
        for (size_t i = 0; i < input_schema_.size(); ++i) {
            std::string name = input_schema_[i].name;
            bool match = qualified && name == column;
            size_t dot = name.find('.');
            if (!qualified && dot != std::string::npos) name = name.substr(dot + 1);
            if (!qualified) match = name == column;
            if (match) {
                col_indices_.push_back(static_cast<int>(i));
                output_schema_.push_back(input_schema_[i]);
                break;
            }
        }
    }
}

void ProjectionOperator::Open() { child_->Open(); }

TupleBatch ProjectionOperator::Next() {
    TupleBatch input = child_->Next();
    if (input.IsEmpty()) return input;
    TupleBatch output;
    for (const auto& row : input.rows) {
        Row projected;
        for (int index : col_indices_) projected.push_back(row[index]);
        output.AddRow(std::move(projected));
    }
    return output;
}

void ProjectionOperator::Close() { child_->Close(); }
Schema ProjectionOperator::GetOutputSchema() const { return output_schema_; }

} // namespace minidb
