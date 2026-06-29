#include "sql/QueryEngine.h"

#include <algorithm>
#include <stdexcept>

namespace sql {

Table QueryEngine::execute(const SelectStatement& statement, const Table& input) const {
    Table result;
    result.columns = statement.selectAll ? input.columns : statement.columns;

    // Validate the projection up front so a typo fails clearly rather than
    // per-row deep inside the loop.
    if (!statement.selectAll) {
        for (const std::string& column : result.columns) {
            if (std::find(input.columns.begin(), input.columns.end(), column) == input.columns.end()) {
                throw std::runtime_error("unknown column in projection: " + column);
            }
        }
    }

    for (const Row& row : input.rows) {
        if (statement.where && !statement.where->eval(row)) {
            continue;
        }
        Row projected;
        for (const std::string& column : result.columns) {
            projected.set(column, row.at(column));
        }
        result.rows.push_back(std::move(projected));
    }

    return result;
}

}  // namespace sql
