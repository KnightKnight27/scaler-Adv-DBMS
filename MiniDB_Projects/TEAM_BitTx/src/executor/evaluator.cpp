#include "executor/evaluator.h"

#include <cstdint>
#include <string>

namespace minidb {

using namespace std;

namespace {
int64_t ToInt(const Value& v) {
  switch (v.GetTypeId()) {
  case TypeId::BIGINT:
    return v.GetAsBigInt();
  case TypeId::INTEGER:
    return v.GetAsInteger();
  case TypeId::BOOLEAN:
    return v.GetAsBoolean() ? 1 : 0;
  case TypeId::VARCHAR: {
    try {
      return stoll(v.GetAsVarchar());
    } catch (...) {
      return 0;
    }
  }
  default:
    return 0;
  }
}
} // namespace

Value Evaluator::Eval(const Expr& e, const vector<Value>& row, const Schema* schema) {
  if (auto l = dynamic_cast<const Literal*>(&e))
    return l->v;
  if (auto c = dynamic_cast<const ColumnRef*>(&e)) {
    if (schema) {
      int idx = schema->GetColumnIndex(c->columnName);
      if (idx >= 0 && static_cast<size_t>(idx) < row.size())
        return row[idx];
    }
    return Value();
  }
  if (auto b = dynamic_cast<const BinaryOp*>(&e)) {
    Value l = b->lhs ? Eval(*b->lhs, row, schema) : Value();
    Value r = b->rhs ? Eval(*b->rhs, row, schema) : Value();
    if (b->op == "+")
      return Value(ToInt(l) + ToInt(r));
    if (b->op == "-")
      return Value(ToInt(l) - ToInt(r));
    if (b->op == "*")
      return Value(ToInt(l) * ToInt(r));
    if (b->op == "/") {
      if (ToInt(r) == 0)
        return Value();
      return Value(ToInt(l) / ToInt(r));
    }
    if (b->op == "=")
      return Value(ToInt(l) == ToInt(r));
    if (b->op == "<")
      return Value(ToInt(l) < ToInt(r));
    if (b->op == ">")
      return Value(ToInt(l) > ToInt(r));
    if (b->op == "AND")
      return Value(ToInt(l) != 0 && ToInt(r) != 0);
    if (b->op == "OR")
      return Value(ToInt(l) != 0 || ToInt(r) != 0);
  }
  return Value();
}

} // namespace minidb