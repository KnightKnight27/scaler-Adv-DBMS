#include "execution/expr_eval.h"

#include "common/exception.h"

namespace minidb {

namespace {

bool is_number(const Value& v) {
    return std::holds_alternative<std::int64_t>(v) || std::holds_alternative<double>(v);
}
double as_number(const Value& v) {
    if (std::holds_alternative<std::int64_t>(v)) return static_cast<double>(std::get<std::int64_t>(v));
    return std::get<double>(v);
}

// Three-way compare two values. Numbers compare numerically, strings
// lexicographically; mixing the two is an error.
int compare_values(const Value& a, const Value& b) {
    if (is_number(a) && is_number(b)) {
        double x = as_number(a), y = as_number(b);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
        int c = std::get<std::string>(a).compare(std::get<std::string>(b));
        return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
    throw DBException("expr: cannot compare values of different types");
}

} // namespace

int resolve_column(const OutSchema& schema, const std::string& table, const std::string& name) {
    int found = -1;
    for (std::size_t i = 0; i < schema.size(); ++i) {
        if (schema[i].name != name) continue;
        if (!table.empty() && schema[i].table != table) continue;
        if (found != -1) throw DBException("expr: ambiguous column '" + name + "'");
        found = static_cast<int>(i);
    }
    if (found == -1) throw DBException("expr: unknown column '" + name + "'");
    return found;
}

Value eval_scalar(const Expr* e, const Tuple& t, const OutSchema& schema) {
    switch (e->kind()) {
        case ExprKind::Literal:
            return static_cast<const LiteralExpr*>(e)->value;
        case ExprKind::Column: {
            const auto* c = static_cast<const ColumnExpr*>(e);
            int idx = resolve_column(schema, c->table, c->name);
            return t.values[static_cast<std::size_t>(idx)];
        }
        default:
            throw DBException("expr: expected a column or literal operand");
    }
}

bool eval_predicate(const Expr* e, const Tuple& t, const OutSchema& schema) {
    if (e->kind() != ExprKind::Binary)
        throw DBException("expr: predicate must be a comparison/AND/OR");
    const auto* b = static_cast<const BinaryExpr*>(e);

    if (b->op == "AND") return eval_predicate(b->left.get(), t, schema) &&
                                eval_predicate(b->right.get(), t, schema);
    if (b->op == "OR")  return eval_predicate(b->left.get(), t, schema) ||
                                eval_predicate(b->right.get(), t, schema);

    Value l = eval_scalar(b->left.get(), t, schema);
    Value r = eval_scalar(b->right.get(), t, schema);
    int c = compare_values(l, r);
    if (b->op == "=")  return c == 0;
    if (b->op == "!=") return c != 0;
    if (b->op == "<")  return c < 0;
    if (b->op == ">")  return c > 0;
    if (b->op == "<=") return c <= 0;
    if (b->op == ">=") return c >= 0;
    throw DBException("expr: unknown operator '" + b->op + "'");
}

} // namespace minidb
