#include "optimizer.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace minidb {

// Compare the unqualified tail of a (possibly "table.col") reference.
static std::string unqualified(const std::string& col) {
    auto dot = col.find('.');
    return dot == std::string::npos ? col : col.substr(dot + 1);
}

bool Optimizer::findPkEquality(const Expr* e, const std::string& pk_col, int64_t& key) {
    if (!e) return false;
    if (e->kind == Expr::Kind::CMP) {
        if (e->op == Expr::Op::EQ && e->val.type == Type::INT &&
            unqualified(e->col) == pk_col) {
            key = e->val.i;
            return true;
        }
        return false;
    }
    if (e->kind == Expr::Kind::AND)
        return findPkEquality(e->left.get(), pk_col, key) ||
               findPkEquality(e->right.get(), pk_col, key);
    // Under OR we cannot safely use a single-key index lookup.
    return false;
}

double Optimizer::selectivity(const Expr* e, const Table* t) {
    if (!e) return 1.0;
    const std::string pk = t->schema.column(0).name;
    size_t n = std::max<size_t>(t->index->size(), 1);
    switch (e->kind) {
        case Expr::Kind::CMP:
            switch (e->op) {
                case Expr::Op::EQ:
                    // The primary key is unique: pk = const hits exactly 1/N.
                    return unqualified(e->col) == pk ? 1.0 / n : 0.1;
                case Expr::Op::NE: return 0.9;
                default: return 0.33; // range predicate
            }
        case Expr::Kind::AND:
            return selectivity(e->left.get(), t) * selectivity(e->right.get(), t);
        case Expr::Kind::OR: {
            double a = selectivity(e->left.get(), t), b = selectivity(e->right.get(), t);
            return a + b - a * b; // inclusion-exclusion
        }
    }
    return 1.0;
}

Optimizer::ScanChoice Optimizer::chooseScan(const Table* t, const Expr* where, bool verbose) {
    ScanChoice choice;
    size_t n = t->index->size();
    double seq_cost = static_cast<double>(n);

    // Estimate how many rows the WHERE clause keeps (drives the cost model and
    // is surfaced to the user). For an index scan, the number of tuples fetched
    // is the estimated matches of the sargable pk predicate.
    double sel = selectivity(where, t);
    double est_rows = std::max(1.0, n * sel);

    int64_t key = 0;
    bool sargable = findPkEquality(where, t->schema.column(0).name, key);
    if (sargable) {
        double matches = std::max(1.0, n * (1.0 / std::max<size_t>(n, 1))); // pk unique -> ~1
        double idx_cost = matches + std::log2(std::max<size_t>(n, 1) + 1);
        if (idx_cost < seq_cost) {
            choice.use_index = true;
            choice.key = key;
            if (verbose)
                std::fprintf(stderr,
                    "[opt] %s: sel=%.3f (~%.0f rows); IndexScan pk=%lld (cost=%.1f) < SeqScan (cost=%.1f)\n",
                    t->name.c_str(), sel, est_rows, static_cast<long long>(key), idx_cost, seq_cost);
            return choice;
        }
        if (verbose)
            std::fprintf(stderr,
                "[opt] %s: sel=%.3f (~%.0f rows); SeqScan (cost=%.1f) <= IndexScan pk=%lld (cost=%.1f)\n",
                t->name.c_str(), sel, est_rows, seq_cost, static_cast<long long>(key), idx_cost);
        return choice;
    }
    if (verbose)
        std::fprintf(stderr,
            "[opt] %s: sel=%.3f (~%.0f rows); SeqScan (cost=%.1f), no sargable pk predicate\n",
            t->name.c_str(), sel, est_rows, seq_cost);
    return choice;
}

bool Optimizer::rightIsInner(const Table* left, const Table* right, bool verbose) {
    bool right_inner = right->index->size() <= left->index->size();
    if (verbose) {
        const Table* inner = right_inner ? right : left;
        std::fprintf(stderr, "[opt] join: inner=%s (rows=%zu), outer=%s (rows=%zu)\n",
                    inner->name.c_str(), inner->index->size(),
                    (right_inner ? left : right)->name.c_str(),
                    (right_inner ? left : right)->index->size());
    }
    return right_inner;
}

} // namespace minidb
