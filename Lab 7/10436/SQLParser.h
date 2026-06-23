#pragma once
#include "Row.h"
#include "ShuntingYard.h"
#include <string>
#include <vector>

struct Query {
    std::vector<std::string> select_cols;  // empty = SELECT *
    std::string from_table;
    std::string where_clause;              // raw string after WHERE, empty if none
};

class SQLParser {
public:
    // Parse a SQL string into a Query struct
    Query parse(const std::string& sql);

    // Execute: filter rows and project columns
    std::vector<Row> execute(const Query& q, const std::vector<Row>& table);

private:
    ShuntingYard evaluator;

    // Evaluate the full WHERE clause against a row
    bool eval_where(const Row& row, const std::string& where);

    // Evaluate a single condition like "age > 20" or "name = 'Alice'"
    bool eval_condition(const Row& row, const std::string& cond);

    // Helpers
    std::string trim(const std::string& s);
    std::vector<std::string> split_by(const std::string& s, const std::string& delim);
    std::string to_upper(const std::string& s);
};
