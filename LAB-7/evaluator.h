#pragma once
#include "types.h"
#include <vector>

class Evaluator {
public:
    static bool evalRPN(const std::vector<RpnToken>& rpn, const Row& row);
    static std::vector<Row> execute(const SelectQuery& query, const std::vector<Row>& data);
    static double getNumeric(const Value& val);
    static std::string getString(const Value& val);
};
