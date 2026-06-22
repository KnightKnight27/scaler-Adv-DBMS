#include "exec/expression.h"

#include <cmath>
#include <cctype>
#include <cstdint>
#include <stdexcept>

namespace axiomdb {

namespace {
bool ieq(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}
}  // namespace

std::optional<size_t> ResultSchema::resolve(const std::string& table, const std::string& col,
                                            bool* ambiguous) const {
  if (ambiguous) *ambiguous = false;
  std::optional<size_t> found;
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (!ieq(columns_[i].name, col)) continue;
    if (!table.empty() && !ieq(columns_[i].table, table)) continue;
    if (found) {  // a second match for an unqualified name -> ambiguous
      if (ambiguous) *ambiguous = true;
      return found;
    }
    found = i;
  }
  return found;
}

ResultSchema ResultSchema::from_table(const Schema& s, const std::string& qualifier) {
  ResultSchema rs;
  for (const Column& c : s.columns()) rs.add(ColumnMeta{qualifier, c.name, c.type});
  return rs;
}

ResultSchema ResultSchema::concat(const ResultSchema& other) const {
  ResultSchema rs = *this;
  for (const ColumnMeta& c : other.columns_) rs.add(c);
  return rs;
}

// ----- coercion -------------------------------------------------------------

std::optional<Value> coerce(const Value& v, TypeId target) {
  if (v.is_null()) return Value::make_null(target);
  if (v.type() == target) return v;
  // Numeric widening / narrowing between Integer and Double.
  if (target == TypeId::Double && v.type() == TypeId::Integer)
    return Value::make_double(static_cast<double>(v.as_integer()));
  if (target == TypeId::Integer && v.type() == TypeId::Double) {
    double d = v.as_double();
    if (d == std::floor(d) && !std::isinf(d)) return Value::make_integer(static_cast<int64_t>(d));
    return std::nullopt;  // non-integral double into an integer column
  }
  return std::nullopt;
}

// ----- evaluation -----------------------------------------------------------

namespace {

// Coerce a non-NULL value to a boolean for logical context.  Throws a clear type
// error for non-boolean/non-numeric operands rather than letting std::get throw
// an opaque std::bad_variant_access (e.g. `WHERE name` where name is VARCHAR).
bool truthy(const Value& v) {
  if (v.type() == TypeId::Boolean) return v.as_boolean();
  if (v.type() == TypeId::Integer || v.type() == TypeId::Double) return v.numeric() != 0.0;
  throw std::runtime_error(std::string("operand must be boolean or numeric, got ") +
                           type_name(v.type()));
}

Value eval_binary(BinOp op, const Value& l, const Value& r) {
  // Logical operators implement SQL three-valued logic.
  if (op == BinOp::And || op == BinOp::Or) {
    // Treat operands as booleans (NULL = unknown).
    auto as_bool = [](const Value& v) -> std::optional<bool> {
      if (v.is_null()) return std::nullopt;
      return truthy(v);
    };
    auto lb = as_bool(l), rb = as_bool(r);
    if (op == BinOp::And) {
      if (lb == false || rb == false) return Value::make_boolean(false);
      if (lb == true && rb == true) return Value::make_boolean(true);
      return Value::make_null(TypeId::Boolean);
    } else {  // Or
      if (lb == true || rb == true) return Value::make_boolean(true);
      if (lb == false && rb == false) return Value::make_boolean(false);
      return Value::make_null(TypeId::Boolean);
    }
  }

  // Any NULL operand makes comparison / arithmetic NULL (unknown).
  if (l.is_null() || r.is_null()) {
    return Value::make_null(op >= BinOp::Eq && op <= BinOp::Ge ? TypeId::Boolean : l.type());
  }

  switch (op) {
    case BinOp::Eq: return Value::make_boolean(l.compare(r) == 0);
    case BinOp::Ne: return Value::make_boolean(l.compare(r) != 0);
    case BinOp::Lt: return Value::make_boolean(l.compare(r) < 0);
    case BinOp::Le: return Value::make_boolean(l.compare(r) <= 0);
    case BinOp::Gt: return Value::make_boolean(l.compare(r) > 0);
    case BinOp::Ge: return Value::make_boolean(l.compare(r) >= 0);
    case BinOp::Add:
    case BinOp::Sub:
    case BinOp::Mul:
    case BinOp::Div: {
      bool both_int = l.type() == TypeId::Integer && r.type() == TypeId::Integer;
      if (both_int) {
        // Checked arithmetic: signed overflow is UB, so on overflow yield NULL
        // (same convention as div-by-zero) rather than wrap silently.
        int64_t a = l.as_integer(), b = r.as_integer(), res;
        switch (op) {
          case BinOp::Add:
            return __builtin_add_overflow(a, b, &res) ? Value::make_null(TypeId::Integer)
                                                      : Value::make_integer(res);
          case BinOp::Sub:
            return __builtin_sub_overflow(a, b, &res) ? Value::make_null(TypeId::Integer)
                                                      : Value::make_integer(res);
          case BinOp::Mul:
            return __builtin_mul_overflow(a, b, &res) ? Value::make_null(TypeId::Integer)
                                                      : Value::make_integer(res);
          case BinOp::Div:
            if (b == 0 || (a == INT64_MIN && b == -1)) return Value::make_null(TypeId::Integer);
            return Value::make_integer(a / b);
          default: break;
        }
      }
      double a = l.numeric(), b = r.numeric();
      switch (op) {
        case BinOp::Add: return Value::make_double(a + b);
        case BinOp::Sub: return Value::make_double(a - b);
        case BinOp::Mul: return Value::make_double(a * b);
        case BinOp::Div:
          if (b == 0.0) return Value::make_null(TypeId::Double);
          return Value::make_double(a / b);
        default: break;
      }
    }
    default: break;
  }
  return Value::make_null(TypeId::Integer);
}

}  // namespace

Value evaluate(const Expr* e, const Row& row, const ResultSchema& schema) {
  switch (e->kind) {
    case ExprKind::Literal:
      return static_cast<const LiteralExpr*>(e)->value;
    case ExprKind::ColumnRef: {
      const auto* c = static_cast<const ColumnRefExpr*>(e);
      bool ambiguous = false;
      auto idx = schema.resolve(c->table, c->column, &ambiguous);
      if (!idx) {
        throw std::runtime_error("unknown column '" +
                                 (c->table.empty() ? c->column : c->table + "." + c->column) + "'");
      }
      if (ambiguous) throw std::runtime_error("ambiguous column '" + c->column + "'");
      return row[*idx];
    }
    case ExprKind::Unary: {
      const auto* u = static_cast<const UnaryExpr*>(e);
      Value v = evaluate(u->operand.get(), row, schema);
      if (u->op == UnOp::Not) {
        if (v.is_null()) return Value::make_null(TypeId::Boolean);
        return Value::make_boolean(!truthy(v));
      }
      // Neg
      if (v.is_null()) return v;
      if (v.type() == TypeId::Integer) {
        int64_t x = v.as_integer();
        return x == INT64_MIN ? Value::make_null(TypeId::Integer) : Value::make_integer(-x);
      }
      return Value::make_double(-v.numeric());
    }
    case ExprKind::Binary: {
      const auto* b = static_cast<const BinaryExpr*>(e);
      Value l = evaluate(b->left.get(), row, schema);
      Value r = evaluate(b->right.get(), row, schema);
      return eval_binary(b->op, l, r);
    }
  }
  return Value::make_null(TypeId::Integer);
}

bool evaluate_predicate(const Expr* e, const Row& row, const ResultSchema& schema) {
  Value v = evaluate(e, row, schema);
  if (v.is_null()) return false;  // unknown -> excluded
  return truthy(v);
}

}  // namespace axiomdb
