#pragma once

#include "common/config.h"
#include "common/types.h"
#include "execution/tuple.h"
#include <string>
#include <vector>

namespace minidb {

enum class SQLStatementType {
    SELECT,
    INSERT,
    DELETE
};

enum class WhereOp {
    EQUALS,
    GREATER_THAN,
    LESS_THAN,
    NONE
};

struct SQLStatement {
    SQLStatementType type;

    // INSERT fields
    std::string insert_table;
    std::vector<Value> insert_values;

    // DELETE fields
    std::string delete_table;

    // SELECT fields
    std::vector<std::string> select_fields;
    std::string select_table;
    std::string join_table;
    std::string join_col_left;
    std::string join_col_right;

    // General WHERE fields
    std::string where_col;
    WhereOp where_op{WhereOp::NONE};
    Value where_val;
};

class SQLParser {
public:
    static SQLStatement Parse(const std::string &sql);

private:
    static std::vector<std::string> Tokenize(const std::string &sql);
};

} // namespace minidb
