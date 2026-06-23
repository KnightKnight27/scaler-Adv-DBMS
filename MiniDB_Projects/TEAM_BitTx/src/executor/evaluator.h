#pragma once

#include "common/types.h"
#include "parser/ast.h"

#include <vector>

namespace minidb {

class Schema;

class Evaluator {
public:
  static Value Eval(const Expr& e, const vector<Value>& row, const Schema* schema);
};

} // namespace minidb