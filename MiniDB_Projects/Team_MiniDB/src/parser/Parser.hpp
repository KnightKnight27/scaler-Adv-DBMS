#pragma once

#include <string>
#include <vector>

using namespace std;

#include "common/Types.hpp"
#include "parser/Lexer.hpp"

namespace minidb {

enum class StmtType { CREATE_TABLE, INSERT, SELECT, DELETE, BEGIN, COMMIT, ROLLBACK, CRASH, RECOVER, UNKNOWN };

struct ColumnSpec { string name; ColumnType type = ColumnType::INT; bool primary_key = false; };
struct WhereClause { string column; CompareOp op = CompareOp::EQ; Value value; };
struct JoinClause { string left_table, right_table, left_column, right_column; };

struct ParsedStatement {
    StmtType type = StmtType::UNKNOWN;
    string table_name;
    vector<ColumnSpec> columns;
    Row insert_row;
    WhereClause where;
    bool has_where = false;
    JoinClause join;
    bool has_join = false;
};

class Parser {
public:
    explicit Parser(const string& sql);
    ParsedStatement parse();

private:
    Lexer lexer_;
    Token current_;
    void advance();
    bool matchKeyword(const string& kw);
    bool expectKeyword(const string& kw);
    Token expect(TokenType type);
    ColumnType parseColumnType(const string& text);
    CompareOp tokenToOp(TokenType t);
};

}  // namespace minidb
