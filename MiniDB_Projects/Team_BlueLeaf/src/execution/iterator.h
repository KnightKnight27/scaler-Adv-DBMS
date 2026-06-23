#pragma once

#include <string>
#include <vector>

#include "catalog/schema.h"
#include "common/types.h"

namespace minidb {

// A tuple flowing through the operator pipeline: just its column values.
struct Tuple {
    std::vector<Value> values;
};

// One output column, carrying its (optional) table qualifier so join outputs can
// disambiguate columns of the same name and qualified references (t.col) resolve.
struct OutColumn {
    std::string table;  // alias/table name, empty if unknown/unqualified
    std::string name;
    ValueType   type;
};
using OutSchema = std::vector<OutColumn>;

// Volcano-style physical operator: pull one tuple at a time (open/next/close).
class Operator {
public:
    virtual ~Operator() = default;
    virtual void open() = 0;
    virtual bool next(Tuple& out) = 0;
    virtual void close() = 0;
    virtual const OutSchema& out_schema() const = 0;
};

} // namespace minidb
