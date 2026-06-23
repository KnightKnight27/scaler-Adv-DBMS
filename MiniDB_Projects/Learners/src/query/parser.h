#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <vector>
#include <string>
#include <vector>
#include "optional.h"

struct JoinClause {
    std::string table;
    std::string left_col;
    std::string right_col;
};

struct WhereClause {
    std::string column;
    std::string op;
    std::string value;
};

struct SQLStatement {
    std::string type; // "SELECT", "INSERT", "DELETE"
    std::string table;
    std::vector<std::string> columns;
    std::vector<std::string> values;
    Optional<JoinClause> join;
    Optional<WhereClause> where;
};

class SQLParser {
private:
    static std::string trim(const std::string& str);
    static Optional<WhereClause> parse_predicate(const std::string& where_str);

public:
    static SQLStatement parse(const std::string& sql);
};

#endif
