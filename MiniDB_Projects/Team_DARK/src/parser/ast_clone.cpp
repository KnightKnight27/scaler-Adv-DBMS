#include "parser/ast.h"

namespace minidb {

std::unique_ptr<Expr> CloneExpr(const Expr* expr) {
    if (expr == nullptr) {
        return nullptr;
    }
    if (const auto* literal = dynamic_cast<const LiteralExpr*>(expr)) {
        return std::make_unique<LiteralExpr>(literal->value);
    }
    if (const auto* column = dynamic_cast<const ColumnRefExpr*>(expr)) {
        return std::make_unique<ColumnRefExpr>(column->name);
    }
    if (const auto* binary = dynamic_cast<const BinaryExpr*>(expr)) {
        return std::make_unique<BinaryExpr>(binary->op, CloneExpr(binary->left.get()),
                                            CloneExpr(binary->right.get()));
    }
    return nullptr;
}

}  // namespace minidb
