#pragma once
#include "Types.h"
#include <string>

class Parser {
public:
    // Execute a minimal SELECT query against the provided table
    static Table executeSelect(const std::string& query, const Table& table);
};
