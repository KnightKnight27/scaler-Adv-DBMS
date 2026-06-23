#include "exec/filter.h"
#include <stdexcept>

namespace minidb {

Filter::Filter(std::unique_ptr<Operator> child, ASTNode* predicate)
    : child_(std::move(child)), predicate_(predicate) {}

void Filter::Open() {
    child_->Open();
}

bool Filter::Next(Tuple& out) {
    while (child_->Next(out)) {
        if (!predicate_ || evaluate(out, predicate_)) {
            return true;
        }
    }
    return false;
}

void Filter::Close() {
    child_->Close();
}

bool Filter::evaluate(const Tuple& tuple, ASTNode* expr) {
    if (!expr) return true;
    
    // Very simplified evaluation logic for demonstration.
    // Real implementation would recursively evaluate AST expression.
    if (expr->type == ASTNodeType::BINARY_EXPR) {
        BinaryExpr* bin = static_cast<BinaryExpr*>(expr);
        // For simplicity, assume left is column, right is literal
        if (bin->left->type == ASTNodeType::COLUMN_REF && bin->right->type == ASTNodeType::LITERAL) {
            ColumnRef* col = static_cast<ColumnRef*>(bin->left.get());
            Literal* lit = static_cast<Literal*>(bin->right.get());
            
            int col_idx = get_schema().find_column(col->column_name);
            if (col_idx == -1) throw std::runtime_error("Column not found in schema");
            
            const Value& tuple_val = tuple.get_value(col_idx);
            int cmp = compare_values(tuple_val, lit->value);
            
            if (bin->op == "=") return cmp == 0;
            if (bin->op == "!=") return cmp != 0;
            if (bin->op == "<") return cmp < 0;
            if (bin->op == ">") return cmp > 0;
            if (bin->op == "<=") return cmp <= 0;
            if (bin->op == ">=") return cmp >= 0;
        }
    }
    return true; // Default fallback for unsupported expressions
}

} // namespace minidb
