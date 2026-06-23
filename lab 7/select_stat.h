#pragma once

#include "expressions.h"
#include <vector>
#include <string>

struct SelectStatement {
    std::vector<std::string> columns; // Empty represents SELECT *
    std::string tableName;
    Expression* whereFilter = nullptr;
    std::string orderByColumn;
    bool orderByAsc = true;
    int limit = -1; // -1 means no limit

    ~SelectStatement() {
        delete whereFilter;
    }
};
