#include "Parser.h"
#include "ShuntingYard.h"
#include <regex>
#include <iostream>
#include <sstream>

// Helper to trim strings
static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}
static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}
static inline void trim(std::string &s) {
    ltrim(s);
    rtrim(s);
}

Table Parser::executeSelect(const std::string& query, const Table& table) {
    // Basic regex: SELECT (.*?) FROM \w+(?: WHERE (.*))?;
    std::regex selectRegex(R"(SELECT\s+(.*?)\s+FROM\s+(\w+)(?:\s+WHERE\s+(.*?))?\s*;?)", std::regex_constants::icase);
    std::smatch match;

    if (!std::regex_match(query, match, selectRegex)) {
        throw std::runtime_error("Invalid SELECT query format");
    }

    std::string selectColsStr = match[1];
    std::string tableName = match[2];
    std::string whereClause = match[3];

    trim(selectColsStr);
    trim(whereClause);

    // Parse select columns
    std::vector<std::string> selectCols;
    if (selectColsStr != "*") {
        std::stringstream ss(selectColsStr);
        std::string col;
        while (std::getline(ss, col, ',')) {
            trim(col);
            selectCols.push_back(col);
        }
    }

    Table result;
    for (const auto& row : table) {
        bool includeRow = true;
        if (!whereClause.empty()) {
            includeRow = ShuntingYard::evaluate(whereClause, row);
        }

        if (includeRow) {
            if (selectCols.empty()) {
                // SELECT *
                result.push_back(row);
            } else {
                Row projectedRow;
                for (const auto& col : selectCols) {
                    auto it = row.find(col);
                    if (it != row.end()) {
                        projectedRow[col] = it->second;
                    } else {
                        throw std::runtime_error("Unknown column in SELECT: " + col);
                    }
                }
                result.push_back(projectedRow);
            }
        }
    }

    return result;
}
