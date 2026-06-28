#include "parser/parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>

namespace minidb {

std::vector<std::string> SQLParser::Tokenize(const std::string &sql) {
    std::vector<std::string> tokens;
    std::string token;
    bool in_quotes = false;
    for (size_t i = 0; i < sql.size(); ++i) {
        char c = sql[i];
        if (c == '\'') {
            if (in_quotes) {
                tokens.push_back(token);
                token.clear();
            }
            in_quotes = !in_quotes;
            continue;
        }
        if (in_quotes) {
            token += c;
            continue;
        }
        if (std::isspace(c)) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else if (c == ',' || c == '=' || c == '<' || c == '>' || c == '(' || c == ')') {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
            tokens.push_back(std::string(1, c));
        } else {
            token += c;
        }
    }
    if (!token.empty()) {
        tokens.push_back(token);
    }
    return tokens;
}
bool IsInteger(const std::string &value) {
    if (value.empty()) return false;

    size_t start = (value[0] == '-') ? 1 : 0;

    if (start == value.size()) return false;

    for (size_t i = start; i < value.size(); i++) {
        if (!std::isdigit(value[i])) {
            return false;
        }
    }

    return true;
}
SQLStatement SQLParser::Parse(const std::string &sql) {
    auto tokens = Tokenize(sql);
    if (tokens.empty()) {
        throw std::runtime_error("Empty SQL query");
    }

    SQLStatement stmt;
    std::string first = tokens[0];
    std::transform(first.begin(), first.end(), first.begin(), ::toupper);

    if (first == "INSERT") {
        stmt.type = SQLStatementType::INSERT;
        // Expect: INSERT INTO <table> VALUES ( val1 , val2 ... )
        if (tokens.size() < 7) throw std::runtime_error("Malformed INSERT statement");
        
        std::string into = tokens[1];
        std::transform(into.begin(), into.end(), into.begin(), ::toupper);
        if (into != "INTO") throw std::runtime_error("Expected INTO keyword");

        stmt.insert_table = tokens[2];
        
        std::string values_kw = tokens[3];
        std::transform(values_kw.begin(), values_kw.end(), values_kw.begin(), ::toupper);
        if (values_kw != "VALUES") throw std::runtime_error("Expected VALUES keyword");

        if (tokens[4] != "(") throw std::runtime_error("Expected opening parenthesis '('");

        size_t idx = 5;
        while (idx < tokens.size() && tokens[idx] != ")") {
            std::string val = tokens[idx];
            // Determine type: integer or varchar
            bool is_num = true;
            for (char c : val) {
                if (!std::isdigit(c) && c != '-') {
                    is_num = false;
                    break;
                }
            }

            if (is_num) {
                stmt.insert_values.push_back(Value(std::stoi(val)));
            } else {
                stmt.insert_values.push_back(Value(val));
            }

            idx++;
            if (idx < tokens.size() && tokens[idx] == ",") {
                idx++; // skip comma
            }
        }
    } else if (first == "DELETE") {
        stmt.type = SQLStatementType::DELETE;
        // Expect: DELETE FROM <table> WHERE <col> = <val>
        if (tokens.size() < 7) throw std::runtime_error("Malformed DELETE statement");
        
        std::string from = tokens[1];
        std::transform(from.begin(), from.end(), from.begin(), ::toupper);
        if (from != "FROM") throw std::runtime_error("Expected FROM keyword");

        stmt.delete_table = tokens[2];

        std::string where_kw = tokens[3];
        std::transform(where_kw.begin(), where_kw.end(), where_kw.begin(), ::toupper);
        if (where_kw != "WHERE") throw std::runtime_error("Expected WHERE keyword");

        stmt.where_col = tokens[4];
        
        std::string op = tokens[5];
        if (op == "=") stmt.where_op = WhereOp::EQUALS;
        else if (op == ">") stmt.where_op = WhereOp::GREATER_THAN;
        else if (op == "<") stmt.where_op = WhereOp::LESS_THAN;
        else throw std::runtime_error("Unsupported comparison operator: " + op);

        std::string val = tokens[6];
        bool is_num = true;
        for (char c : val) {
            if (!std::isdigit(c) && c != '-') {
                is_num = false;
                break;
            }
        }
        if (is_num) stmt.where_val = Value(std::stoi(val));
        else stmt.where_val = Value(val);

    } else if (first == "SELECT") {
        stmt.type = SQLStatementType::SELECT;
        // Expect: SELECT <fields> FROM <table> [JOIN <t2> ON <col1> = <col2>] [WHERE <col> = <val>]
        size_t idx = 1;
        while (idx < tokens.size()) {
            std::string token_upper = tokens[idx];
            std::transform(token_upper.begin(), token_upper.end(), token_upper.begin(), ::toupper);
            if (token_upper == "FROM") {
                break;
            }
            if (tokens[idx] != ",") {
                stmt.select_fields.push_back(tokens[idx]);
            }
            idx++;
        }

        if (idx >= tokens.size()) throw std::runtime_error("Malformed SELECT: missing FROM");
        idx++; // skip FROM
        
        if (idx >= tokens.size()) throw std::runtime_error("Malformed SELECT: missing table name");
        stmt.select_table = tokens[idx];
        idx++;

        // Check for JOIN
        if (idx < tokens.size()) {
            std::string join_kw = tokens[idx];
            std::transform(join_kw.begin(), join_kw.end(), join_kw.begin(), ::toupper);
            if (join_kw == "JOIN") {
                idx++;
                if (idx >= tokens.size()) throw std::runtime_error("Expected table name after JOIN");
                stmt.join_table = tokens[idx];
                idx++;

                if (idx >= tokens.size()) throw std::runtime_error("Expected ON after JOIN table");
                std::string on_kw = tokens[idx];
                std::transform(on_kw.begin(), on_kw.end(), on_kw.begin(), ::toupper);
                if (on_kw != "ON") throw std::runtime_error("Expected ON after JOIN table");
                idx++;

                if (idx + 2 >= tokens.size()) throw std::runtime_error("Malformed ON clause");
                stmt.join_col_left = tokens[idx];
                idx++;
                
                if (tokens[idx] != "=") throw std::runtime_error("Expected = in JOIN ON clause");
                idx++;

                stmt.join_col_right = tokens[idx];
                idx++;
            }
        }

        // Check for WHERE
        if (idx < tokens.size()) {
            std::string where_kw = tokens[idx];
            std::transform(where_kw.begin(), where_kw.end(), where_kw.begin(), ::toupper);
            if (where_kw == "WHERE") {
                idx++;
                if (idx + 2 >= tokens.size()) throw std::runtime_error("Malformed WHERE clause");
                stmt.where_col = tokens[idx];
                idx++;

                std::string op = tokens[idx];
                if (op == "=") stmt.where_op = WhereOp::EQUALS;
                else if (op == ">") stmt.where_op = WhereOp::GREATER_THAN;
                else if (op == "<") stmt.where_op = WhereOp::LESS_THAN;
                else throw std::runtime_error("Unsupported comparison operator: " + op);
                idx++;

                std::string val = tokens[idx];
                bool is_num = true;
                for (char c : val) {
                    if (!std::isdigit(c) && c != '-') {
                        is_num = false;
                        break;
                    }
                }
                if (is_num) stmt.where_val = Value(std::stoi(val));
                else stmt.where_val = Value(val);
                idx++;
            }
        }
    } else {
        throw std::runtime_error("Unsupported SQL statement: " + tokens[0]);
    }

    return stmt;
}

} // namespace minidb
