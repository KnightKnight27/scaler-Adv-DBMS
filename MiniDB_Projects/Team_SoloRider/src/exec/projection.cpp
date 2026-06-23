#include "exec/projection.h"

namespace minidb {

Projection::Projection(std::unique_ptr<Operator> child, const std::vector<int>& column_indices)
    : child_(std::move(child)), column_indices_(column_indices) {
    
    std::vector<Column> out_cols;
    const Schema& child_schema = child_->get_schema();
    for (int idx : column_indices_) {
        out_cols.push_back(child_schema.columns[idx]);
    }
    output_schema_ = Schema(out_cols);
}

void Projection::Open() {
    child_->Open();
}

bool Projection::Next(Tuple& out) {
    Tuple child_tuple;
    if (child_->Next(child_tuple)) {
        std::vector<Value> out_values;
        for (int idx : column_indices_) {
            out_values.push_back(child_tuple.get_value(idx));
        }
        out = Tuple(std::move(out_values), child_tuple.rid);
        return true;
    }
    return false;
}

void Projection::Close() {
    child_->Close();
}

} // namespace minidb
