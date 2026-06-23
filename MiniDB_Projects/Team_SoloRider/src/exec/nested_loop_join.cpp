#include "exec/nested_loop_join.h"
#include <stdexcept>

namespace minidb {

NestedLoopJoin::NestedLoopJoin(std::unique_ptr<Operator> left, std::unique_ptr<Operator> right, ASTNode* condition)
    : left_(std::move(left)), right_(std::move(right)), condition_(condition), has_left_(false) {
    
    std::vector<Column> out_cols = left_->get_schema().columns;
    const auto& right_cols = right_->get_schema().columns;
    out_cols.insert(out_cols.end(), right_cols.begin(), right_cols.end());
    output_schema_ = Schema(out_cols);
}

void NestedLoopJoin::Open() {
    left_->Open();
    right_->Open();
    has_left_ = left_->Next(left_tuple_);
}

bool NestedLoopJoin::Next(Tuple& out) {
    while (has_left_) {
        Tuple right_tuple;
        while (right_->Next(right_tuple)) {
            if (!condition_ || evaluate_join(left_tuple_, right_tuple)) {
                std::vector<Value> out_vals = left_tuple_.values;
                out_vals.insert(out_vals.end(), right_tuple.values.begin(), right_tuple.values.end());
                out = Tuple(std::move(out_vals));
                return true;
            }
        }
        
        right_->Close();
        right_->Open(); // Rewind inner loop
        
        has_left_ = left_->Next(left_tuple_);
    }
    return false;
}

void NestedLoopJoin::Close() {
    left_->Close();
    right_->Close();
}

bool NestedLoopJoin::evaluate_join(const Tuple& left_tuple, const Tuple& right_tuple) {
    if (!condition_) return true;
    
    if (condition_->type == ASTNodeType::BINARY_EXPR) {
        BinaryExpr* bin = static_cast<BinaryExpr*>(condition_);
        if (bin->left->type == ASTNodeType::COLUMN_REF && bin->right->type == ASTNodeType::COLUMN_REF) {
            ColumnRef* left_col = static_cast<ColumnRef*>(bin->left.get());
            ColumnRef* right_col = static_cast<ColumnRef*>(bin->right.get());
            
            int l_idx = left_->get_schema().find_column(left_col->column_name);
            int r_idx = right_->get_schema().find_column(right_col->column_name);
            
            if (l_idx != -1 && r_idx != -1) {
                int cmp = compare_values(left_tuple.get_value(l_idx), right_tuple.get_value(r_idx));
                if (bin->op == "=") return cmp == 0;
            }
        }
    }
    return true; // Simplified for demo
}

} // namespace minidb
