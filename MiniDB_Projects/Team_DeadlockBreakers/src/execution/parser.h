#pragma once

#include "execution/ast.h"
#include <memory>
#include <string>

namespace minidb {

/*
 * Simple SQL parser using string splitting.
 * Supports:
 *   CREATE TABLE name (col1 TYPE, col2 TYPE, ...)
 *   INSERT INTO table VALUES (v1, v2, ...)
 *   SELECT col1, col2 FROM table WHERE col = value
 *   SELECT * FROM t1 JOIN t2 ON t1.col = t2.col [WHERE ...]
 *   DELETE FROM table WHERE col = value
 *
 * WHERE supports:  =  !=  <  <=  >  >=  AND  OR
 */
class Parser {
public:
    // Parse a SQL string into an AST statement
    static std::unique_ptr<Statement> Parse(const std::string& sql);

private:
    static std::unique_ptr<CreateTableStmt> ParseCreate(const std::string& sql);
    static std::unique_ptr<SelectStmt>      ParseSelect(const std::string& sql);
    static std::unique_ptr<InsertStmt>      ParseInsert(const std::string& sql);
    static std::unique_ptr<DeleteStmt>      ParseDelete(const std::string& sql);

    // Parse a WHERE clause string into a WhereClause (handles AND/OR)
    static WhereClause ParseWhereClause(const std::string& where_str);

    // Parse a column type string (INT, DOUBLE, STRING, VARCHAR, TEXT, FLOAT, REAL)
    static ColumnType ParseColumnType(const std::string& type_str);

    // Helpers
    static std::string Trim(const std::string& s);
    static std::string ToUpper(const std::string& s);
    static std::vector<std::string> Split(const std::string& s, char delim);
    static Predicate ParsePredicate(const std::string& s);
    static Value ParseValue(const std::string& s);
};

} // namespace minidb
