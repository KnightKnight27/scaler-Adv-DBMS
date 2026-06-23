#include "exec/eval.h"
#include <stdexcept>

namespace minidb {

int resolve_column(const Schema& schema, const ColumnRef& ref) {
  if (!ref.table.empty()) {
    int idx = schema.index_of(ref.table + "." + ref.column);
    if (idx >= 0) return idx;
  }
  int idx = schema.index_of(ref.column);
  if (idx >= 0) return idx;
  const std::string suffix = "." + ref.column;
  for (int i = 0; i < static_cast<int>(schema.columns.size()); ++i) {
    const std::string& name = schema.columns[i].name;
    if (name.size() >= suffix.size() &&
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
      return i;
  }
  throw std::runtime_error("unknown column: " + ref.column);
}

int compare_values(const Value& a, const Value& b) {
  if (a.index() != b.index()) throw std::runtime_error("type mismatch in comparison");
  if (std::holds_alternative<int64_t>(a)) {
    int64_t x = std::get<int64_t>(a), y = std::get<int64_t>(b);
    return x < y ? -1 : (x > y ? 1 : 0);
  }
  const std::string& x = std::get<std::string>(a);
  const std::string& y = std::get<std::string>(b);
  return x < y ? -1 : (x > y ? 1 : 0);
}

Value eval_value(const Expr* expr, const Row& row, const Schema& schema) {
  if (auto* c = dynamic_cast<const ColumnRef*>(expr)) return row[resolve_column(schema, *c)];
  if (auto* i = dynamic_cast<const IntLit*>(expr))    return Value{i->value};
  if (auto* s = dynamic_cast<const StrLit*>(expr))    return Value{s->value};
  throw std::runtime_error("expected a value expression");
}

bool eval_predicate(const Expr* expr, const Row& row, const Schema& schema) {
  const auto* bin = dynamic_cast<const BinaryExpr*>(expr);
  if (!bin) throw std::runtime_error("expected a boolean predicate");

  if (bin->op == "AND")
    return eval_predicate(bin->left.get(), row, schema) &&
           eval_predicate(bin->right.get(), row, schema);
  if (bin->op == "OR")
    return eval_predicate(bin->left.get(), row, schema) ||
           eval_predicate(bin->right.get(), row, schema);

  int cmp = compare_values(eval_value(bin->left.get(), row, schema),
                           eval_value(bin->right.get(), row, schema));
  if (bin->op == "=")  return cmp == 0;
  if (bin->op == "!=") return cmp != 0;
  if (bin->op == "<")  return cmp < 0;
  if (bin->op == ">")  return cmp > 0;
  if (bin->op == "<=") return cmp <= 0;
  if (bin->op == ">=") return cmp >= 0;
  throw std::runtime_error("unknown operator: " + bin->op);
}

}  // namespace minidb
