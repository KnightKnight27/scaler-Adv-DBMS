#pragma once
#include "shunting_yard.h"
#include <string>
#include <vector>

struct ParsedQuery {
    bool isSelectAll = false;
    std::vector<std::string> selectColumns;
    std::string tableName;
    bool hasWhere = false;
    std::vector<Token> whereInfix;
    std::vector<Token> whereRPN;
    bool isValid = true;
    std::string errorMessage = "";
};

// Parse a minimal SQL SELECT statement:
// SELECT (col1, col2, ... | *) FROM table_name [WHERE condition]
ParsedQuery parseSQL(const std::string& sql);
