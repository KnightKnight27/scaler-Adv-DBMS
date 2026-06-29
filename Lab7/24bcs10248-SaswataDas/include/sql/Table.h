#pragma once

#include <string>
#include <vector>

#include "Row.h"

namespace sql {

// An in-memory relation: the ordered column names plus the rows. The
// query engine reads from and produces values of this type, satisfying
// the "over vector<Row>" requirement.
struct Table {
    std::vector<std::string> columns;
    std::vector<Row> rows;
};

}  // namespace sql
