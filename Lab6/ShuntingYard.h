#pragma once
#include "Types.h"
#include <string>

class ShuntingYard {
public:
    // Evaluate a WHERE clause expression against a given Row
    static bool evaluate(const std::string& expression, const Row& row);
};
