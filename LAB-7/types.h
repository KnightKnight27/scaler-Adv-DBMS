#pragma once
#include <string>
#include <vector>
#include <unordered_map>

enum class TokenType {
    SELECT, FROM, WHERE, ORDER, BY, ASC, DESC, LIMIT,
    AND, OR, NOT,
    IDENTIFIER, NUMBER, STRING,
    GT, LT, GTE, LTE, EQ, NEQ,
    COMMA, ASTERISK, OPERATOR,
    LPAREN, RPAREN, END
};

struct Token {
    TokenType type;
    std::string text;
};

struct Value {
    enum Type { DOUBLE, STRING } type;
    double d_val;
    std::string s_val;

    Value() : type(DOUBLE), d_val(0.0), s_val("") {}
    Value(double d) : type(DOUBLE), d_val(d), s_val("") {}
    Value(std::string s) : type(STRING), d_val(0.0), s_val(std::move(s)) {}
    Value(const char* s) : type(STRING), d_val(0.0), s_val(s) {}
};

struct Row {
    std::unordered_map<std::string, Value> cols;
};

enum class RpnTokenType {
    LITERAL,
    COLUMN_REF,
    OPERATOR
};

struct RpnToken {
    RpnTokenType type;
    Value val;
};

struct SelectQuery {
    std::vector<std::string> columns; // Empty = SELECT *
    std::string from;
    std::string where_raw;
    std::string order_by;
    bool order_asc = true;
    int limit = -1;
};