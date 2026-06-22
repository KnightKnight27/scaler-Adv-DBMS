#pragma once

#include <string>
#include <variant>

namespace sql {

// A cell value. The minimal engine supports numeric and textual data,
// which is enough to demonstrate projection and filtering.
using Value = std::variant<double, std::string>;

// Ordering / equality used by WHERE comparisons. Comparing values of
// different kinds (number vs. text) is a type error and throws.
bool valueEqual(const Value& lhs, const Value& rhs);
bool valueLess(const Value& lhs, const Value& rhs);

std::string toString(const Value& value);

}  // namespace sql
