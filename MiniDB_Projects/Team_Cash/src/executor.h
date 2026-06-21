// Query execution: the Volcano (iterator) model.
//
// Every operator is a pull-based iterator: a parent calls next(row) on its
// child to get one row at a time. The leaves (SeqScan / IndexScan) read from
// the heap file. Each operator exposes outputColumns - the fully-qualified
// ("table.column") names of the columns in the rows it produces - so operators
// above can resolve a column by name.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "catalog.h"
#include "optimizer.h"
#include "parser.h"
#include "types.h"

namespace minidb {

// Find the index of a ColRef within a list of "table.column" names.
int resolveColumn(const std::vector<std::string>& outputColumns, const ColRef& ref);
// Compare two values with one of = != < > <= >=.
bool valueCompare(const Value& a, const std::string& op, const Value& b);

class Operator {
public:
    virtual ~Operator() = default;
    virtual bool next(Row& out) = 0;
    std::vector<std::string> outputColumns;
};

// Build the operator tree for a plan. Returns the root operator.
std::unique_ptr<Operator> buildOperator(const PlanNode* node, Catalog& catalog);

}  // namespace minidb
