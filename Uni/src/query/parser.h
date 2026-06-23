#pragma once

#include <string>
#include <vector>

enum class SQLStatementType {
    INVALID,
    SELECT,
    INSERT,
    DELETE
};

struct SQLWhereCondition {
    std::string column;
    std::string op; // "=", ">", "<"
    std::string value;
    bool has_condition = false;
};

struct SQLJoinCondition {
    std::string join_table;
    std::string left_col;  // e.g., "users.id"
    std::string right_col; // e.g., "orders.user_id"
    bool has_join = false;
};

struct SQLStatement {
    SQLStatementType type = SQLStatementType::INVALID;
    std::string table_name;

    // For SELECT
    std::vector<std::string> fields; // ["*"] or specific column names
    SQLJoinCondition join;
    SQLWhereCondition where;

    // For INSERT
    std::vector<std::string> insert_values;
    
    std::string raw_query;
};

class SQLParser {
public:
    static SQLStatement Parse(const std::string& sql);
};
