#include "sql_parser.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

// Helper to check if a string is numeric
static bool isNumericString(const std::string& str) {
    if (str.empty()) return false;
    size_t offset = 0;
    if (str[0] == '-' || str[0] == '+') offset = 1;
    bool hasDot = false;
    for (size_t i = offset; i < str.length(); ++i) {
        if (str[i] == '.') {
            if (hasDot) return false;
            hasDot = true;
        } else if (!std::isdigit(str[i])) {
            return false;
        }
    }
    return true;
}

// Tokenize the SQL statement into individual words and operators
std::vector<std::string> tokenizeSQL(const std::string& sql) {
    std::vector<std::string> tokens;
    size_t i = 0;
    size_t len = sql.length();
    while (i < len) {
        if (std::isspace(sql[i])) {
            i++;
            continue;
        }

        // Single-quoted string literals (preserve spaces inside quotes)
        if (sql[i] == '\'') {
            std::string s = "'";
            i++;
            while (i < len && sql[i] != '\'') {
                s += sql[i];
                i++;
            }
            if (i < len && sql[i] == '\'') {
                s += "'";
                i++;
            }
            tokens.push_back(s);
            continue;
        }

        // Semicolon
        if (sql[i] == ';') {
            tokens.push_back(";");
            i++;
            continue;
        }

        // Comma
        if (sql[i] == ',') {
            tokens.push_back(",");
            i++;
            continue;
        }

        // Parentheses
        if (sql[i] == '(' || sql[i] == ')') {
            tokens.push_back(std::string(1, sql[i]));
            i++;
            continue;
        }

        // Multi-character operators
        if (i + 1 < len) {
            std::string op2 = sql.substr(i, 2);
            if (op2 == ">=" || op2 == "<=" || op2 == "!=" || op2 == "<>") {
                tokens.push_back(op2);
                i += 2;
                continue;
            }
        }

        // Single-character operators
        char op1 = sql[i];
        if (op1 == '>' || op1 == '<' || op1 == '=' || op1 == '+' || op1 == '-' || op1 == '*' || op1 == '/') {
            tokens.push_back(std::string(1, op1));
            i++;
            continue;
        }

        // Standard words, numbers, and identifiers
        std::string word;
        while (i < len && !std::isspace(sql[i]) && sql[i] != ',' && sql[i] != ';' && sql[i] != '\'' &&
               sql[i] != '(' && sql[i] != ')' && sql[i] != '=' && sql[i] != '>' && sql[i] != '<' &&
               sql[i] != '+' && sql[i] != '-' && sql[i] != '*' && sql[i] != '/') {
            word += sql[i];
            i++;
        }
        if (!word.empty()) {
            tokens.push_back(word);
        }
    }
    return tokens;
}

// Convert a SQL string token to an expression Token
Token sqlTokenToExprToken(const std::string& t) {
    if (t.length() >= 2 && t.front() == '\'' && t.back() == '\'') {
        return {TokenType::LITERAL_STRING, t.substr(1, t.length() - 2)};
    }
    if (t == "(" || t == ")") {
        return {TokenType::PARENTHESIS, t};
    }
    
    std::string upperT = t;
    std::transform(upperT.begin(), upperT.end(), upperT.begin(), ::toupper);

    if (upperT == "AND" || upperT == "OR" || upperT == "NOT" ||
        t == ">" || t == "<" || t == "=" || t == ">=" || t == "<=" || t == "!=" || t == "<>" ||
        t == "+" || t == "-" || t == "*" || t == "/") {
        return {TokenType::OPERATOR, upperT};
    }

    if (isNumericString(t)) {
        return {TokenType::LITERAL_NUMBER, t};
    }

    return {TokenType::IDENTIFIER, t};
}

ParsedQuery parseSQL(const std::string& sql) {
    ParsedQuery result;
    std::vector<std::string> tokens = tokenizeSQL(sql);

    if (tokens.empty()) {
        result.isValid = false;
        result.errorMessage = "Empty query statement.";
        return result;
    }

    size_t idx = 0;
    
    // 1. Expect SELECT
    std::string selectKeyword = tokens[idx];
    std::transform(selectKeyword.begin(), selectKeyword.end(), selectKeyword.begin(), ::toupper);
    if (selectKeyword != "SELECT") {
        result.isValid = false;
        result.errorMessage = "Syntax Error: Queries must begin with SELECT.";
        return result;
    }
    idx++;

    // 2. Parse Projection Columns
    if (idx >= tokens.size()) {
        result.isValid = false;
        result.errorMessage = "Syntax Error: Expected columns list after SELECT.";
        return result;
    }

    if (tokens[idx] == "*") {
        result.isSelectAll = true;
        idx++;
    } else {
        while (idx < tokens.size()) {
            std::string keyword = tokens[idx];
            std::transform(keyword.begin(), keyword.end(), keyword.begin(), ::toupper);
            if (keyword == "FROM") {
                break;
            }
            
            // Add column identifier
            if (tokens[idx] != ",") {
                result.selectColumns.push_back(tokens[idx]);
            }
            idx++;
        }
    }

    // 3. Expect FROM
    if (idx >= tokens.size()) {
        result.isValid = false;
        result.errorMessage = "Syntax Error: Expected FROM keyword.";
        return result;
    }
    std::string fromKeyword = tokens[idx];
    std::transform(fromKeyword.begin(), fromKeyword.end(), fromKeyword.begin(), ::toupper);
    if (fromKeyword != "FROM") {
        result.isValid = false;
        result.errorMessage = "Syntax Error: Expected FROM keyword, found '" + tokens[idx] + "'.";
        return result;
    }
    idx++;

    // 4. Parse Table Name
    if (idx >= tokens.size()) {
        result.isValid = false;
        result.errorMessage = "Syntax Error: Expected table name after FROM.";
        return result;
    }
    result.tableName = tokens[idx];
    idx++;

    // 5. Parse optional WHERE
    if (idx < tokens.size()) {
        std::string whereKeyword = tokens[idx];
        std::transform(whereKeyword.begin(), whereKeyword.end(), whereKeyword.begin(), ::toupper);
        
        if (whereKeyword == ";") {
            // Semicolon terminating query
            return result;
        }
        
        if (whereKeyword != "WHERE") {
            result.isValid = false;
            result.errorMessage = "Syntax Error: Unexpected token '" + tokens[idx] + "' after table name.";
            return result;
        }
        idx++; // Skip WHERE

        // Extract condition tokens
        result.hasWhere = true;
        std::vector<std::string> whereTokens;
        while (idx < tokens.size() && tokens[idx] != ";") {
            whereTokens.push_back(tokens[idx]);
            idx++;
        }

        if (whereTokens.empty()) {
            result.isValid = false;
            result.errorMessage = "Syntax Error: Expected condition expression after WHERE.";
            return result;
        }

        // Map SQL tokens to expression tokens
        for (const auto& t : whereTokens) {
            result.whereInfix.push_back(sqlTokenToExprToken(t));
        }

        // Compile Infix to Postfix RPN using Shunting-Yard
        try {
            result.whereRPN = infixToPostfix(result.whereInfix);
        } catch (const std::exception& e) {
            result.isValid = false;
            result.errorMessage = "Parser Error (Shunting-Yard): " + std::string(e.what());
        }
    }

    return result;
}
