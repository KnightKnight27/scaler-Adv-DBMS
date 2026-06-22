#pragma once
#include <string>
#include <cstdint>
#include "ast.hpp"
#include "../catalog/catalog.hpp"

namespace minidb {

// A small cost-based optimizer. It estimates predicate selectivity, chooses
// between a sequential scan and a primary-key index scan, and orders a join so
// the smaller relation is the (materialised) inner. Every decision is printed
// as an "[opt] ..." line so the choice is visible during the demo.
class Optimizer {
public:
    struct ScanChoice {
        bool use_index = false;
        int64_t key = 0;       // the pk value to look up, when use_index
    };

    // Decide how to scan `t` given its WHERE clause (may be null). Prints the
    // cost comparison.
    static ScanChoice chooseScan(const Table* t, const Expr* where, bool verbose = true);

    // Estimate the fraction of rows of `t` that satisfy `e` (0..1).
    static double selectivity(const Expr* e, const Table* t);

    // For a two-table join, returns true if the right (joined) table should be
    // the inner relation (i.e. it is the smaller one). Prints the choice.
    static bool rightIsInner(const Table* left, const Table* right, bool verbose = true);

private:
    // If `e` contains a usable "pk = const" predicate reachable through AND
    // nodes (never under OR), return true and set `key`.
    static bool findPkEquality(const Expr* e, const std::string& pk_col, int64_t& key);
};

} // namespace minidb
