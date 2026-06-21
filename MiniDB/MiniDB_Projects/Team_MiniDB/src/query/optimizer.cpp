#include "optimizer.hpp"

#include <algorithm>

#include "../catalog/catalog.hpp"

namespace {
// Default selectivities when we can't do better (no histograms).
constexpr double EQ_DEFAULT    = 0.10;  // col = const
constexpr double RANGE_DEFAULT = 0.33;  // col < / > / <= / >= const

bool is_pk(const ColumnRef* col, const Table& t) {
    return col && col->name == t.schema.columns[static_cast<std::size_t>(t.pk_col)].name;
}
}  // namespace

double estimate_selectivity(const Expr* pred, const Table& t) {
    const auto* b = dynamic_cast<const BinaryExpr*>(pred);
    if (!b) return 1.0;

    if (b->op == "AND")  // independent predicates: probabilities multiply
        return estimate_selectivity(b->left.get(), t) * estimate_selectivity(b->right.get(), t);
    if (b->op == "OR") {  // inclusion-exclusion: P(A∪B) = P(A)+P(B)-P(A)P(B)
        double a = estimate_selectivity(b->left.get(), t);
        double c = estimate_selectivity(b->right.get(), t);
        return std::min(1.0, a + c - a * c);
    }

    // comparison: <column> <op> <literal>
    const auto* col = dynamic_cast<const ColumnRef*>(b->left.get());
    bool pk = is_pk(col, t);
    double n = std::max(1.0, static_cast<double>(t.row_count));

    if (b->op == "=")  return pk ? 1.0 / n : EQ_DEFAULT;            // PK eq → exactly 1 row
    if (b->op == "!=") return pk ? std::max(0.0, 1.0 - 1.0 / n) : (1.0 - EQ_DEFAULT);
    if (b->op == "<" || b->op == ">" || b->op == "<=" || b->op == ">=") return RANGE_DEFAULT;
    return 1.0;
}

double estimate_cardinality(const Table& t, const Expr* pred) {
    double n = std::max(1.0, static_cast<double>(t.row_count));
    double sel = pred ? estimate_selectivity(pred, t) : 1.0;
    return std::max(1.0, n * sel);
}
