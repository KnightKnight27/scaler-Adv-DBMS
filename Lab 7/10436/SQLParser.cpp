#include "SQLParser.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

// ---- helpers ----

std::string SQLParser::trim(const std::string& s) {
    size_t l = s.find_first_not_of(" \t\r\n");
    if (l == std::string::npos) return "";
    size_t r = s.find_last_not_of(" \t\r\n");
    return s.substr(l, r - l + 1);
}

std::string SQLParser::to_upper(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c){ return (char)std::toupper(c); });
    return out;
}

std::vector<std::string> SQLParser::split_by(const std::string& s, const std::string& delim) {
    std::vector<std::string> parts;
    size_t pos = 0, found;
    while ((found = s.find(delim, pos)) != std::string::npos) {
        parts.push_back(trim(s.substr(pos, found - pos)));
        pos = found + delim.size();
    }
    parts.push_back(trim(s.substr(pos)));
    return parts;
}

// ---- parse ----

Query SQLParser::parse(const std::string& sql) {
    Query q;
    std::string up = to_upper(sql);

    size_t sel_pos  = up.find("SELECT");
    size_t from_pos = up.find(" FROM ");
    size_t where_pos = up.find(" WHERE ");

    if (sel_pos == std::string::npos || from_pos == std::string::npos)
        throw std::invalid_argument("Invalid SQL: missing SELECT or FROM");

    // SELECT columns
    std::string cols_str = trim(sql.substr(sel_pos + 6, from_pos - sel_pos - 6));
    if (to_upper(cols_str) != "*") {
        auto parts = split_by(cols_str, ",");
        for (auto& p : parts)
            q.select_cols.push_back(trim(p));
    }

    // FROM table
    size_t table_start = from_pos + 6;  // skip " FROM "
    size_t table_end   = (where_pos != std::string::npos) ? where_pos : sql.size();
    q.from_table = trim(sql.substr(table_start, table_end - table_start));

    // WHERE clause (keep original case for column/value matching)
    if (where_pos != std::string::npos)
        q.where_clause = trim(sql.substr(where_pos + 7));

    return q;
}

// ---- eval_condition ----

bool SQLParser::eval_condition(const Row& row, const std::string& cond) {
    // Detect operator (order matters: <= >= != before < > =)
    std::string ops[] = {"<=", ">=", "!=", "<", ">", "="};
    std::string op;
    size_t op_pos = std::string::npos;

    for (auto& candidate : ops) {
        size_t p = cond.find(candidate);
        if (p != std::string::npos) {
            op     = candidate;
            op_pos = p;
            break;
        }
    }
    if (op.empty()) return false;

    std::string col = trim(cond.substr(0, op_pos));
    std::string val = trim(cond.substr(op_pos + op.size()));

    auto it = row.find(col);
    if (it == row.end()) return false;

    const std::string& row_val = it->second;

    // String comparison: value is quoted
    if (!val.empty() && val.front() == '\'') {
        std::string unquoted = val.substr(1, val.size() - 2);
        if (op == "=")  return row_val == unquoted;
        if (op == "!=") return row_val != unquoted;
        // Lexicographic for < > <= >= on strings
        if (op == "<")  return row_val < unquoted;
        if (op == ">")  return row_val > unquoted;
        if (op == "<=") return row_val <= unquoted;
        if (op == ">=") return row_val >= unquoted;
        return false;
    }

    // Numeric comparison (val may be an arithmetic expression)
    double rhs;
    try {
        rhs = evaluator.evaluate(val);
    } catch (...) {
        return false;
    }

    double lhs;
    try {
        lhs = std::stod(row_val);
    } catch (...) {
        return false;
    }

    if (op == "=")  return lhs == rhs;
    if (op == "!=") return lhs != rhs;
    if (op == "<")  return lhs <  rhs;
    if (op == ">")  return lhs >  rhs;
    if (op == "<=") return lhs <= rhs;
    if (op == ">=") return lhs >= rhs;
    return false;
}

// ---- eval_where ----

bool SQLParser::eval_where(const Row& row, const std::string& where) {
    // Split on " OR " first (lower precedence)
    auto or_parts = split_by(where, " OR ");
    for (auto& or_part : or_parts) {
        // All AND sub-conditions must hold
        auto and_parts = split_by(or_part, " AND ");
        bool all_true = true;
        for (auto& cond : and_parts) {
            if (!eval_condition(row, trim(cond))) { all_true = false; break; }
        }
        if (all_true) return true;
    }
    return false;
}

// ---- execute ----

std::vector<Row> SQLParser::execute(const Query& q, const std::vector<Row>& table) {
    std::vector<Row> result;
    for (const auto& row : table) {
        if (!q.where_clause.empty() && !eval_where(row, q.where_clause))
            continue;

        if (q.select_cols.empty()) {
            result.push_back(row);
        } else {
            Row projected;
            for (const auto& col : q.select_cols) {
                auto it = row.find(col);
                if (it != row.end())
                    projected[col] = it->second;
            }
            result.push_back(projected);
        }
    }
    return result;
}
