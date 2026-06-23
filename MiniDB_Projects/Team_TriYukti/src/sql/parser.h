#pragma once
#include <string>
#include <vector>

namespace minidb {

enum class StatementType { 
    SELECT, INSERT, CREATE_TABLE, DELETE, EXPLAIN, 
    SHOW_LOCKS, SHOW_TRANSACTIONS, SHOW_TABLES, BEGIN, COMMIT, ROLLBACK, UNKNOWN, EMPTY 
};

struct ParsedStatement {
    StatementType type;
    std::string table_name;
    
    // Create
    std::vector<std::pair<std::string, std::string>> columns_with_type;
    
    // Select
    std::vector<std::string> columns;
    
    // Insert
    std::vector<std::string> values;
    
    // Where
    bool has_where = false;
    std::string where_column;
    std::string where_op;
    std::string where_value;
    
    // Join
    bool has_join = false;
    std::string join_table;
    std::string join_cond_left;
    std::string join_cond_right;
};

class Parser {
public:
    static ParsedStatement Parse(const std::string &sql);
};

} // namespace minidb
