#include "Expression.h"
#include <cassert>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

BinaryExpression::BinaryExpression(Expression *leftA, Expression *rightA)
    : left(leftA), right(rightA) {}

AndExpression::AndExpression(Expression *leftA, Expression *rightA)
    : BinaryExpression(leftA, rightA) {}

Value AndExpression::solve() {
  auto leftResult = left->solve();
  auto rightResult = right->solve();

  auto isTruthy = [](const Value &result) -> bool {
    return std::visit(
        [](const auto &v) {
          using T = std::decay_t<decltype(v)>;

          if constexpr (std::is_same_v<T, bool>) {
            return v;
          } else if constexpr (std::is_same_v<T, std::string>) {
            return !v.empty();
          } else if constexpr (std::is_same_v<T, int>) {
            return v > 0;
          }
        },
        result);
  };

  bool leftTruthy = isTruthy(leftResult);

  if (!leftTruthy) {
    return false;
  }

  bool rightTruthy = isTruthy(rightResult);
  return rightTruthy;
}

EqualityExpression::EqualityExpression(Expression *leftA, Expression *rightA)
    : BinaryExpression(leftA, rightA) {}

Value EqualityExpression::solve() {
  auto leftResult = left->solve();
  auto rightResult = right->solve();

  if (leftResult.index() != rightResult.index()) {
    throw std::runtime_error(
        "[Parser] Equality comparision between two different types");
  }

  return std::visit(
      [](const auto &leftValue, const auto &rightValue) {
        using T1 = std::decay_t<decltype(leftValue)>;
        using T2 = std::decay_t<decltype(rightValue)>;

        if constexpr (std::is_same_v<T1, T2>) {
          return leftValue == rightValue;
        } else {
          // We'll never reach here because of the early throw
          return false;
        }
      },
      leftResult, rightResult);
}

LiteralExpression::LiteralExpression(Value &&valueA)
    : value(std::move(valueA)) {}

Value LiteralExpression::solve() { return value; }

IdentifierExpression::IdentifierExpression(const std::string &columnNameA)
    : columnName(columnNameA) {}

Value IdentifierExpression::solve() {
  // TODO: To be implemented
  return "SHAHI PANEER > KADAHI PANEER";
}
