#pragma once

#include "common/types.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <regex>
#include <sstream>

namespace minidb {

// sql parser — recursive-descent parser for a minimal sql subset
// supported statements:
//   create table name (col1 int, col2 float, col3 string, primary key (colx))
//   insert into name values (v1, v2, ...)
//   select col1, col2 from name [where col op val] [join name2 on cola = colb]
//   delete from name [where col op val]

enum class TokenType {
    KEYWORD, IDENTIFIER, NUMBER, FLOAT_NUM, STRING,
    LPAREN, RPAREN, COMMA, SEMICOLON,
    EQUALS, NOT_EQUALS, LT, GT, LTE, GTE,
    STAR, DOT, END
};

struct Token {
    TokenType type;
    std::string text;
};

// ast nodes
struct ColumnDef {
    std::string name;
    ValueType   type;   // int, float, string
};

struct CreateTableStmt {
    std::string            table_name;
    std::vector<ColumnDef> columns;
    std::string            primary_key;
};

struct InsertStmt {
    std::string table_name;
    Record      values;
};

struct WhereClause {
    std::string column;
    std::string op;     // =, !=, <, >, <=, >=
    Value       value;
    bool        has_where = false;
};

struct JoinClause {
    std::string table_name;
    std::string left_col;
    std::string right_col;
    bool        has_join = false;
};

struct SelectStmt {
    std::vector<std::string> columns;  // empty or ["*"] = select *
    std::string              table_name;
    WhereClause              where;
    JoinClause               join;
};

struct DeleteStmt {
    std::string table_name;
    WhereClause where;
};

// union type for parsed statements
struct Statement {
    enum Type { CREATE, INSERT, SELECT, DELETE, NONE } type = NONE;
    CreateTableStmt create;
    InsertStmt      insert;
    SelectStmt      select;
    DeleteStmt      del;
};

class Parser {
public:
    Parser() = default;

    // parse a single sql statement string.  returns a statement.
    // on error, stmt.type == none.
    Statement parse(const std::string& sql);

    // get the last error message.
    const std::string& error() const { return _error; }

private:
    // tokeniser
    std::vector<Token> tokenize(const std::string& sql);

    // recursive-descent parsing functions
    bool parse_statement(Statement& stmt, const std::vector<Token>& tokens, size_t& pos);
    bool parse_create(Statement& stmt, const std::vector<Token>& tokens, size_t& pos);
    bool parse_insert(Statement& stmt, const std::vector<Token>& tokens, size_t& pos);
    bool parse_select(Statement& stmt, const std::vector<Token>& tokens, size_t& pos);
    bool parse_delete(Statement& stmt, const std::vector<Token>& tokens, size_t& pos);
    bool parse_where(WhereClause& wc, const std::vector<Token>& tokens, size_t& pos);
    bool parse_join(JoinClause& jc, const std::vector<Token>& tokens, size_t& pos);
    bool parse_value(Value& val, const std::vector<Token>& tokens, size_t& pos);

    ValueType parse_column_type(const std::string& type_str);

    std::string _error;
};

// implementation

inline Statement Parser::parse(const std::string& sql) {
    Statement stmt;
    _error.clear();

    auto tokens = tokenize(sql);
    if (tokens.empty()) {
        _error = "Empty input";
        return stmt;
    }

    size_t pos = 0;
    if (!parse_statement(stmt, tokens, pos)) {
        stmt.type = Statement::NONE;
    }
    return stmt;
}

inline std::vector<Token> Parser::tokenize(const std::string& sql) {
    std::vector<Token> tokens;
    size_t i = 0;

    while (i < sql.size()) {
        char c = sql[i];

        // skip whitespace
        if (isspace(c)) { ++i; continue; }

        // single-char tokens
        if (c == '(') { tokens.push_back({TokenType::LPAREN, "("}); ++i; continue; }
        if (c == ')') { tokens.push_back({TokenType::RPAREN, ")"}); ++i; continue; }
        if (c == ',') { tokens.push_back({TokenType::COMMA, ","}); ++i; continue; }
        if (c == ';') { tokens.push_back({TokenType::SEMICOLON, ";"}); ++i; continue; }
        if (c == '*') { tokens.push_back({TokenType::STAR, "*"}); ++i; continue; }
        if (c == '.') { tokens.push_back({TokenType::DOT, "."}); ++i; continue; }

        // multi-char operators
        if (c == '=') { tokens.push_back({TokenType::EQUALS, "="}); ++i; continue; }
        if (c == '!' && i+1 < sql.size() && sql[i+1] == '=') {
            tokens.push_back({TokenType::NOT_EQUALS, "!="}); i += 2; continue;
        }
        if (c == '<') {
            if (i+1 < sql.size() && sql[i+1] == '=') {
                tokens.push_back({TokenType::LTE, "<="}); i += 2;
            } else {
                tokens.push_back({TokenType::LT, "<"}); ++i;
            }
            continue;
        }
        if (c == '>') {
            if (i+1 < sql.size() && sql[i+1] == '=') {
                tokens.push_back({TokenType::GTE, ">="}); i += 2;
            } else {
                tokens.push_back({TokenType::GT, ">"}); ++i;
            }
            continue;
        }

        // string literals
        if (c == '\'' || c == '\"') {
            char quote = c; ++i;
            std::string str;
            while (i < sql.size() && sql[i] != quote) {
                str += sql[i++];
            }
            if (i < sql.size()) ++i; // skip closing quote
            tokens.push_back({TokenType::STRING, str});
            continue;
        }

        // numbers
        if (isdigit(c) || (c == '-' && i+1 < sql.size() && isdigit(sql[i+1]))) {
            std::string num;
            bool is_float = false;
            while (i < sql.size() && (isdigit(sql[i]) || sql[i] == '.' || sql[i] == '-')) {
                if (sql[i] == '.') is_float = true;
                num += sql[i++];
            }
            tokens.push_back({is_float ? TokenType::FLOAT_NUM : TokenType::NUMBER, num});
            continue;
        }

        // identifiers and keywords
        if (isalpha(c) || c == '_') {
            std::string word;
            while (i < sql.size() && (isalnum(sql[i]) || sql[i] == '_')) {
                word += sql[i++];
            }
            std::string upper = to_upper(word);
            if (upper == "CREATE" || upper == "TABLE" || upper == "INSERT" ||
                upper == "INTO"   || upper == "VALUES" || upper == "SELECT" ||
                upper == "FROM"   || upper == "WHERE"  || upper == "JOIN"   ||
                upper == "ON"     || upper == "DELETE"  || upper == "INT"   ||
                upper == "FLOAT"  || upper == "STRING"  || upper == "KEY"   ||
                upper == "PRIMARY"|| upper == "AND"     || upper == "OR") {
                tokens.push_back({TokenType::KEYWORD, upper});
            } else {
                tokens.push_back({TokenType::IDENTIFIER, word});
            }
            continue;
        }

        // unknown char — skip
        ++i;
    }

    tokens.push_back({TokenType::END, ""});
    return tokens;
}

inline bool Parser::parse_statement(Statement& stmt,
    const std::vector<Token>& tokens, size_t& pos) {
    if (pos >= tokens.size()) return false;

    std::string kw = tokens[pos].text;
    if (kw == "CREATE") return parse_create(stmt, tokens, pos);
    if (kw == "INSERT") return parse_insert(stmt, tokens, pos);
    if (kw == "SELECT") return parse_select(stmt, tokens, pos);
    if (kw == "DELETE") return parse_delete(stmt, tokens, pos);

    _error = "Unknown statement: " + kw;
    return false;
}

inline bool Parser::parse_create(Statement& stmt,
    const std::vector<Token>& tokens, size_t& pos) {
    // create table name (col1 type, col2 type, ..., primary key (col))
    stmt.type = Statement::CREATE;
    auto& s = stmt.create;

    if (tokens[pos++].text != "CREATE") return false;
    if (pos >= tokens.size() || tokens[pos++].text != "TABLE") return false;
    if (pos >= tokens.size() || tokens[pos].type != TokenType::IDENTIFIER) {
        _error = "Expected table name"; return false;
    }
    s.table_name = tokens[pos++].text;
    if (pos >= tokens.size() || tokens[pos++].text != "(") {
        _error = "Expected '('"; return false;
    }

    while (pos < tokens.size() && tokens[pos].text != ")") {
        if (tokens[pos].text == "PRIMARY") {
            // primary key (col)
            pos++; // primary
            if (pos >= tokens.size() || tokens[pos++].text != "KEY") return false;
            if (pos >= tokens.size() || tokens[pos++].text != "(") return false;
            if (pos >= tokens.size() || tokens[pos].type != TokenType::IDENTIFIER) {
                _error = "Expected primary key column"; return false;
            }
            s.primary_key = tokens[pos++].text;
            if (pos >= tokens.size() || tokens[pos++].text != ")") return false;
            break;
        }

        if (tokens[pos].type != TokenType::IDENTIFIER) {
            _error = "Expected column name"; return false;
        }
        ColumnDef col;
        col.name = tokens[pos++].text;

        if (pos >= tokens.size() || tokens[pos].type != TokenType::KEYWORD) {
            _error = "Expected column type"; return false;
        }
        col.type = parse_column_type(tokens[pos++].text);
        s.columns.push_back(col);

        if (pos < tokens.size() && tokens[pos].text == ",") pos++;
    }

    if (pos >= tokens.size() || tokens[pos++].text != ")") {
        _error = "Expected ')'"; return false;
    }
    return true;
}

inline ValueType Parser::parse_column_type(const std::string& type_str) {
    if (type_str == "INT")    return ValueType::INT32;
    if (type_str == "FLOAT")  return ValueType::FLOAT64;
    if (type_str == "STRING") return ValueType::STRING;
    return ValueType::STRING;
}

inline bool Parser::parse_insert(Statement& stmt,
    const std::vector<Token>& tokens, size_t& pos) {
    // insert into name values (v1, v2, ...)
    stmt.type = Statement::INSERT;
    auto& s = stmt.insert;

    if (tokens[pos++].text != "INSERT") return false;
    if (pos >= tokens.size() || tokens[pos++].text != "INTO") return false;
    if (pos >= tokens.size() || tokens[pos].type != TokenType::IDENTIFIER) {
        _error = "Expected table name"; return false;
    }
    s.table_name = tokens[pos++].text;
    if (pos >= tokens.size() || tokens[pos++].text != "VALUES") return false;
    if (pos >= tokens.size() || tokens[pos++].text != "(") return false;

    while (pos < tokens.size() && tokens[pos].text != ")") {
        Value val;
        if (!parse_value(val, tokens, pos)) return false;
        s.values.push_back(val);
        if (pos < tokens.size() && tokens[pos].text == ",") pos++;
    }

    if (pos >= tokens.size() || tokens[pos++].text != ")") {
        _error = "Expected ')'"; return false;
    }
    return true;
}

inline bool Parser::parse_value(Value& val,
    const std::vector<Token>& tokens, size_t& pos) {
    if (pos >= tokens.size()) return false;
    auto t = tokens[pos];
    if (t.type == TokenType::NUMBER) {
        val = Value(std::stoi(t.text));
    } else if (t.type == TokenType::FLOAT_NUM) {
        val = Value(std::stod(t.text));
    } else if (t.type == TokenType::STRING) {
        val = Value(t.text);
    } else {
        _error = "Expected value"; return false;
    }
    ++pos;
    return true;
}

inline bool Parser::parse_select(Statement& stmt,
    const std::vector<Token>& tokens, size_t& pos) {
    // select col1, col2, ... from table [where ...] [join ...]
    stmt.type = Statement::SELECT;
    auto& s = stmt.select;

    if (tokens[pos++].text != "SELECT") return false;

    // column list
    while (pos < tokens.size() && tokens[pos].text != "FROM") {
        if (tokens[pos].type == TokenType::STAR) {
            s.columns = {"*"};
            pos++;
            break;
        }
        if (tokens[pos].type == TokenType::IDENTIFIER) {
            s.columns.push_back(tokens[pos++].text);
            if (pos < tokens.size() && tokens[pos].text == ",") pos++;
            continue;
        }
        _error = "Expected column name"; return false;
    }

    if (pos >= tokens.size() || tokens[pos++].text != "FROM") return false;
    if (pos >= tokens.size() || tokens[pos].type != TokenType::IDENTIFIER) {
        _error = "Expected table name"; return false;
    }
    s.table_name = tokens[pos++].text;

    // optional where
    if (pos < tokens.size() && tokens[pos].text == "WHERE") {
        if (!parse_where(s.where, tokens, pos)) return false;
    }

    // optional join
    if (pos < tokens.size() && tokens[pos].text == "JOIN") {
        if (!parse_join(s.join, tokens, pos)) return false;

        // join may have its own where via on
        if (pos < tokens.size() && tokens[pos].text == "WHERE") {
            // for simplicity, we treat on conditions as the where for the join
            // (handled inside parse_join)
        }
    }

    return true;
}

inline bool Parser::parse_where(WhereClause& wc,
    const std::vector<Token>& tokens, size_t& pos) {
    wc.has_where = true;
    if (tokens[pos++].text != "WHERE") return false;
    if (pos >= tokens.size() || tokens[pos].type != TokenType::IDENTIFIER) {
        _error = "Expected column in WHERE"; return false;
    }
    wc.column = tokens[pos++].text;

    if (pos >= tokens.size()) return false;
    wc.op = tokens[pos++].text;

    if (!parse_value(wc.value, tokens, pos)) return false;
    return true;
}

inline bool Parser::parse_join(JoinClause& jc,
    const std::vector<Token>& tokens, size_t& pos) {
    jc.has_join = true;
    if (tokens[pos++].text != "JOIN") return false;
    if (pos >= tokens.size() || tokens[pos].type != TokenType::IDENTIFIER) {
        _error = "Expected table name after JOIN"; return false;
    }
    jc.table_name = tokens[pos++].text;

    if (pos >= tokens.size() || tokens[pos++].text != "ON") return false;

    if (pos >= tokens.size() || tokens[pos].type != TokenType::IDENTIFIER) {
        _error = "Expected column in ON"; return false;
    }
    jc.left_col = tokens[pos++].text;

    if (pos >= tokens.size() || tokens[pos++].text != "=") return false;

    if (pos >= tokens.size() || tokens[pos].type != TokenType::IDENTIFIER) {
        _error = "Expected column in ON"; return false;
    }
    jc.right_col = tokens[pos++].text;

    return true;
}

inline bool Parser::parse_delete(Statement& stmt,
    const std::vector<Token>& tokens, size_t& pos) {
    // delete from table [where ...]
    stmt.type = Statement::DELETE;
    auto& s = stmt.del;

    if (tokens[pos++].text != "DELETE") return false;
    if (pos >= tokens.size() || tokens[pos++].text != "FROM") return false;
    if (pos >= tokens.size() || tokens[pos].type != TokenType::IDENTIFIER) {
        _error = "Expected table name"; return false;
    }
    s.table_name = tokens[pos++].text;

    if (pos < tokens.size() && tokens[pos].text == "WHERE") {
        if (!parse_where(s.where, tokens, pos)) return false;
    }
    return true;
}

} // namespace minidb
