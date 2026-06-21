#pragma once

#include "ast.hpp"

struct Table;

// Cost-based optimizer helpers. We have no histograms, so selectivity is
// estimated with standard textbook heuristics, refined by one fact we do know
// exactly: the primary key is unique, so `pk = const` matches exactly one row.
// The planner (in executor.cpp) uses these to choose SeqScan vs IndexScan and
// to order a join's inputs.

// Fraction of `t`'s rows expected to satisfy `pred` (in (0, 1]).
double estimate_selectivity(const Expr* pred, const Table& t);

// Expected number of rows `t` yields after applying `pred` (>= 1). With no
// predicate, this is the table's row count.
double estimate_cardinality(const Table& t, const Expr* pred);
