#pragma once

#include "ast.hpp"

struct Table;

// fraction of rows matching pred, in (0,1]
double estimate_selectivity(const Expr* pred, const Table& t);

// expected rows after pred (>= 1)
double estimate_cardinality(const Table& t, const Expr* pred);
