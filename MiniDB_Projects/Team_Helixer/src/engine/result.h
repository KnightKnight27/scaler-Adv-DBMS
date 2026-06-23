#pragma once
#include <string>
#include <vector>
#include "common/value.h"

namespace minidb {

// The outcome of executing one SQL statement: either a result set (column
// headers + rows) for SELECT, or just a status message for DDL/DML. `ok` is
// false when execution failed (the message explains why).
struct QueryResult {
    bool                     ok{true};
    std::string              message;   // status or error text
    std::vector<std::string> columns;   // header for SELECT
    std::vector<Tuple>       rows;       // result rows for SELECT

    static QueryResult error(const std::string &m) { return {false, m, {}, {}}; }
    static QueryResult status(const std::string &m) { return {true, m, {}, {}}; }
};

} // namespace minidb
