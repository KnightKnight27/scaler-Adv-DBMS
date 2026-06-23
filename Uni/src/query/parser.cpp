#include "query/parser.h"
#include <sstream>
#include <algorithm>
#include <iostream>

// Helper to trim whitespaces
static std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Helper to convert to lowercase
static std::string ToLower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}

// Simple tokenizer that respects single quotes (for string values)
static std::vector<std::string> Tokenize(const std::string& sql) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quotes = false;

    for (size_t i = 0; i < sql.length(); ++i) {
        char c = sql[i];
        if (c == '\'') {
            in_quotes = !in_quotes;
            current += c;
        } else if (in_quotes) {
            current += c;
        } else if (c == ',' || c == '(' || c == ')' || c == '=' || c == '<' || c == '>') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            tokens.push_back(std::string(1, c));
        } else if (std::isspace(c)) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

SQLStatement SQLParser::Parse(const std::string& sql) {
    SQLStatement stmt;
    stmt.raw_query = sql;
    
    std::vector<std::string> tokens = Tokenize(Trim(sql));
    if (tokens.empty()) return stmt;

    std::string command = ToLower(tokens[0]);

    if (command == "insert") {
        // Expected: INSERT INTO <table_name> VALUES ( <val1> , <val2> ... )
        if (tokens.size() < 6 || ToLower(tokens[1]) != "into") {
            return stmt;
        }
        stmt.type = SQLStatementType::INSERT;
        stmt.table_name = tokens[2];

        size_t idx = 3;
        if (ToLower(tokens[idx]) == "values") {
            idx++;
        } else {
            return stmt;
        }

        if (tokens[idx] == "(") {
            idx++;
        } else {
            return stmt;
        }

        while (idx < tokens.size() && tokens[idx] != ")") {
            std::string val = tokens[idx];
            if (val != ",") {
                // Strip quotes if any
                if (val.front() == '\'' && val.back() == '\'') {
                    val = val.substr(1, val.length() - 2);
                }
                stmt.insert_values.push_back(val);
            }
            idx++;
        }
    } 
    else if (command == "delete") {
        // Expected: DELETE FROM <table_name> [WHERE <col> = <val>]
        if (tokens.size() < 3 || ToLower(tokens[1]) != "from") {
            return stmt;
        }
        stmt.type = SQLStatementType::DELETE;
        stmt.table_name = tokens[2];

        size_t idx = 3;
        if (idx < tokens.size() && ToLower(tokens[idx]) == "where") {
            idx++;
            if (idx + 2 < tokens.size()) {
                stmt.where.has_condition = true;
                stmt.where.column = tokens[idx];
                stmt.where.op = tokens[idx + 1];
                std::string val = tokens[idx + 2];
                if (val.front() == '\'' && val.back() == '\'') {
                    val = val.substr(1, val.length() - 2);
                }
                stmt.where.value = val;
            }
        }
    } 
    else if (command == "select") {
        // Expected: SELECT <fields> FROM <table_name> [JOIN <join_table> ON <lcol> = <rcol>] [WHERE <col> = <val>]
        stmt.type = SQLStatementType::SELECT;
        size_t idx = 1;
        
        // Parse fields
        while (idx < tokens.size() && ToLower(tokens[idx]) != "from") {
            if (tokens[idx] != ",") {
                stmt.fields.push_back(tokens[idx]);
            }
            idx++;
        }

        if (idx >= tokens.size() || ToLower(tokens[idx]) != "from") {
            stmt.type = SQLStatementType::INVALID;
            return stmt;
        }
        idx++; // skip FROM

        if (idx >= tokens.size()) {
            stmt.type = SQLStatementType::INVALID;
            return stmt;
        }
        stmt.table_name = tokens[idx];
        idx++;

        // Parse optional JOIN
        if (idx < tokens.size() && ToLower(tokens[idx]) == "join") {
            idx++;
            if (idx < tokens.size()) {
                stmt.join.has_join = true;
                stmt.join.join_table = tokens[idx];
                idx++;

                if (idx < tokens.size() && ToLower(tokens[idx]) == "on") {
                    idx++;
                    if (idx + 2 < tokens.size() && tokens[idx + 1] == "=") {
                        stmt.join.left_col = tokens[idx];
                        stmt.join.right_col = tokens[idx + 2];
                        idx += 3;
                    }
                }
            }
        }

        // Parse optional WHERE
        if (idx < tokens.size() && ToLower(tokens[idx]) == "where") {
            idx++;
            if (idx + 2 < tokens.size()) {
                stmt.where.has_condition = true;
                stmt.where.column = tokens[idx];
                stmt.where.op = tokens[idx + 1];
                std::string val = tokens[idx + 2];
                if (val.front() == '\'' && val.back() == '\'') {
                    val = val.substr(1, val.length() - 2);
                }
                stmt.where.value = val;
            }
        }
    }

    return stmt;
}
