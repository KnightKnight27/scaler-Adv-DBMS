#pragma once
//
// value.h
// ---------------------------------------------------------------------------
// A `Value` is the unit of data flowing through the engine. It can hold either
// an integer (modelled as long long) or a string. We use std::variant which is
// the C++17 idiomatic tagged-union.
//
// A `Row` maps column-name -> Value, and a `Table` is just a vector<Row>.
// ---------------------------------------------------------------------------

#include <string>
#include <variant>
#include <vector>
#include <map>
#include <stdexcept>

// A SQL cell value: integer OR string.
using Value = std::variant<long long, std::string>;

// A single tuple. We use std::map so column order is deterministic when we
// print "SELECT *", and lookups are simple.
using Row = std::map<std::string, Value>;

// A table is an ordered collection of rows.
using Table = std::vector<Row>;

// ---- small helpers around the variant ------------------------------------

inline bool isInt(const Value &v) { return std::holds_alternative<long long>(v); }
inline bool isStr(const Value &v) { return std::holds_alternative<std::string>(v); }

inline long long asInt(const Value &v) {
    if (!isInt(v)) throw std::runtime_error("Value is not an integer");
    return std::get<long long>(v);
}

inline const std::string &asStr(const Value &v) {
    if (!isStr(v)) throw std::runtime_error("Value is not a string");
    return std::get<std::string>(v);
}

// Render a Value for printing.
inline std::string valueToString(const Value &v) {
    if (isInt(v)) return std::to_string(std::get<long long>(v));
    return std::get<std::string>(v);
}
