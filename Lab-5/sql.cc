// Lab 5 (Part 2) - Minimal SQL SELECT parser + executor (implementation)

#include "sql.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "shunting_yard.h"

namespace lab5 {

namespace {

std::string to_upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Read a column's value as a double for expression evaluation. Strings
// that don't look like numbers evaluate to 0 (this toy supports numeric
// WHERE conditions; string columns can still be selected and printed).
double as_number(const Value& v) {
    if (const double* d = std::get_if<double>(&v)) return *d;
    if (const std::string* s = std::get_if<std::string>(&v)) {
        try {
            std::size_t pos = 0;
            double num = std::stod(*s, &pos);
            if (pos == s->size()) return num;
        } catch (...) {
        }
    }
    return 0.0;
}

}  // namespace

SelectQuery parse_select(const std::string& sql) {
    SelectQuery q;
    std::istringstream ss(sql);
    std::string word;

    ss >> word;
    if (to_upper(word) != "SELECT") {
        throw std::runtime_error("parse_select: query must start with SELECT");
    }

    // Column list until FROM.
    while (ss >> word && to_upper(word) != "FROM") {
        if (!word.empty() && word.back() == ',') word.pop_back();
        if (word == "*") {
            q.columns.clear();   // SELECT *
        } else if (!word.empty()) {
            q.columns.push_back(word);
        }
    }
    if (to_upper(word) != "FROM") {
        throw std::runtime_error("parse_select: missing FROM");
    }

    if (!(ss >> q.table)) {
        throw std::runtime_error("parse_select: missing table name");
    }

    // Optional clauses: WHERE ... [ORDER BY ...] [LIMIT n]
    // We read the rest into words and process them in order.
    std::vector<std::string> rest;
    while (ss >> word) rest.push_back(word);

    std::size_t i = 0;
    while (i < rest.size()) {
        std::string kw = to_upper(rest[i]);

        if (kw == "WHERE") {
            ++i;
            std::string clause;
            while (i < rest.size() && to_upper(rest[i]) != "ORDER" &&
                   to_upper(rest[i]) != "LIMIT") {
                clause += (clause.empty() ? "" : " ") + rest[i];
                ++i;
            }
            q.where_clause = clause;
        } else if (kw == "ORDER") {
            ++i;
            if (i < rest.size() && to_upper(rest[i]) == "BY") ++i;
            if (i < rest.size()) q.order_by = rest[i++];
            if (i < rest.size()) {
                std::string dir = to_upper(rest[i]);
                if (dir == "ASC" || dir == "DESC") {
                    q.order_asc = (dir == "ASC");
                    ++i;
                }
            }
        } else if (kw == "LIMIT") {
            ++i;
            if (i < rest.size()) {
                q.limit = std::stoi(rest[i++]);
            }
        } else {
            throw std::runtime_error("parse_select: unexpected token '" +
                                     rest[i] + "'");
        }
    }
    return q;
}

std::vector<Row> execute(const SelectQuery& q, const std::vector<Row>& data) {
    // Compile the WHERE clause once (not per row).
    std::vector<std::string> rpn;
    if (!q.where_clause.empty()) {
        rpn = to_rpn(tokenize(q.where_clause));
    }

    std::vector<Row> result;

    for (const Row& row : data) {
        // 1. Filter (WHERE).
        if (!rpn.empty()) {
            std::unordered_map<std::string, double> vars;
            for (const auto& [name, val] : row.cols) vars[name] = as_number(val);
            if (eval_rpn(rpn, vars) == 0.0) continue;   // condition false
        }

        // 2. Project (SELECT columns).
        if (q.columns.empty()) {
            result.push_back(row);   // SELECT *
        } else {
            Row projected;
            for (const std::string& col : q.columns) {
                auto it = row.cols.find(col);
                if (it != row.cols.end()) projected.cols[col] = it->second;
            }
            result.push_back(std::move(projected));
        }
    }

    // 3. Sort (ORDER BY).
    if (!q.order_by.empty()) {
        const std::string& key = q.order_by;
        bool asc = q.order_asc;
        std::stable_sort(result.begin(), result.end(),
                         [&](const Row& a, const Row& b) {
                             auto ia = a.cols.find(key);
                             auto ib = b.cols.find(key);
                             double va = (ia != a.cols.end()) ? as_number(ia->second) : 0.0;
                             double vb = (ib != b.cols.end()) ? as_number(ib->second) : 0.0;
                             return asc ? va < vb : va > vb;
                         });
    }

    // 4. Truncate (LIMIT).
    if (q.limit >= 0 && static_cast<int>(result.size()) > q.limit) {
        result.resize(q.limit);
    }

    return result;
}

void print_rows(const std::vector<Row>& rows) {
    if (rows.empty()) {
        std::cout << "  (no rows)\n";
        return;
    }
    for (const Row& row : rows) {
        std::cout << "  ";
        // Print columns in a stable (alphabetical) order so output is
        // deterministic regardless of the hash map layout.
        std::vector<std::string> names;
        names.reserve(row.cols.size());
        for (const auto& [name, _] : row.cols) names.push_back(name);
        std::sort(names.begin(), names.end());

        for (const std::string& name : names) {
            const Value& v = row.cols.at(name);
            std::cout << name << "=";
            if (const double* d = std::get_if<double>(&v)) std::cout << *d;
            else if (const std::string* s = std::get_if<std::string>(&v)) std::cout << *s;
            std::cout << "  ";
        }
        std::cout << "\n";
    }
}

}  // namespace lab5
