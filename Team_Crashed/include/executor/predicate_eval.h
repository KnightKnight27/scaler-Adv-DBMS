// =============================================================================
// include/executor/predicate_eval.h
// -----------------------------------------------------------------------------
// Shared helpers for evaluating parser::Expr nodes against an in-memory
// Tuple. Originally lived inside src/executor/seq_scan.cpp's anonymous
// namespace; the DeleteExecutor needed the same primitives to find victim
// rows, so they were lifted here.
//
// Nothing in this header is executor-specific. The Tuple / Value / Schema
// types come from the catalog and executor public headers. Callers stay
// in charge of the Schema that defines the row layout.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "catalog/schema.h"
#include "executor/executor.h"
#include "parser/ast.h"

namespace minidb::executor {

// ----- Schema-aware row helpers -----

// WHAT: the byte footprint of a single column inside a fixed-size row image.
// WHY : matches the encoding InsertExecutor uses so we can decode a raw
//       HeapFile record back into a Tuple the same way the scan does.
std::size_t colBytes(const catalog::Column& c);

// WHAT: decode one column of a raw row image into a Value.
// WHY : exposed for DeleteExecutor so it can read a row it pulled by
//       RecordId without re-implementing the schema-aware layout.
Value decodeColumn(const catalog::Column& c, const std::uint8_t* base);

// WHAT: decode an entire row image into a Tuple.
// WHY : shared between SeqScanExecutor and DeleteExecutor.
void decodeRow(std::span<const std::uint8_t> bytes,
               const catalog::Schema& schema,
               Tuple& out);

// WHAT: return the Value held in the column with the given name.
// WHY : column-name lookup is what evalPredicate needs.
Value resolveColumn(const Tuple& t,
                    const catalog::Schema& schema,
                    const std::string& name);

// WHAT: evaluate a literal expression to a Value. Non-literal expressions
//       fall back to NULL for safety.
// WHY : used inside evalPredicate and by DeleteExecutor when it needs to
//       compare a predicate's right-hand side.
Value evalLiteral(const parser::Expr& e);

// WHAT: three-way Value compare, mirroring SQL semantics for the
//       supported scalar types. NULL is "less than everything" but we
//       return 0 here so callers can branch on the NULL tag separately.
// RETURN: -1 / 0 / 1.
int compareValues(const Value& a, const Value& b);

// WHAT: evaluate a predicate expression over a Tuple. Supports the
//       boolean operators (=, !=, <, <=, >, >=, AND, OR, NOT, IS [NOT]
//       NULL), column references, and literal leaves.
// RETURN: true when the row passes the filter.
bool evalPredicate(const parser::Expr& e,
                   const Tuple& t,
                   const catalog::Schema& schema);

// WHAT: deep-clone a parser::Expr tree. Needed when a single AST node
//       must be shared (or saved) across two different owners (e.g. the
//       DeleteExecutor keeps the WHERE clause AND hands a copy to the
//       underlying SeqScan).
// RETURN: a fresh, independently-owned Expr.
std::unique_ptr<parser::Expr> cloneExpr(const parser::Expr& e);

} // namespace minidb::executor
