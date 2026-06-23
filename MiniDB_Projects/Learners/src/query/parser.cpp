#include "parser.h"
#include <regex>
#include <sstream>
#include <algorithm>
#include <iostream>

std::string SQLParser::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

Optional<WhereClause> SQLParser::parse_predicate(const std::string& where_str) {
    // Regex matches col, operator, value (order by descending length of operator)
    std::regex pred_regex(R"(^([\w\.]+)\s*(>=|<=|!=|=|>|<)\s*(.*)$)");
    std::smatch match;
    if (std::regex_match(where_str, match, pred_regex)) {
        std::string col = match[1].str();
        std::string op = match[2].str();
        std::string val_str = trim(match[3].str());
        
        // Strip quotes if string literal
        if ((val_str.front() == '\'' && val_str.back() == '\'') || 
            (val_str.front() == '"' && val_str.back() == '"')) {
            val_str = val_str.substr(1, val_str.length() - 2);
        }
        return WhereClause{col, op, val_str};
    }
    return {};
}

SQLStatement SQLParser::parse(const std::string& sql) {
    std::string clean_sql = trim(sql);
    // Replace multiple spaces/newlines with single space
    clean_sql = std::regex_replace(clean_sql, std::regex(R"(\s+)"), " ");

    SQLStatement stmt;

    // 1. Parse INSERT
    std::regex insert_regex(R"(^INSERT\s+INTO\s+(\w+)\s+VALUES\s*\((.*)\)$)", std::regex_constants::icase);
    std::smatch match;
    if (std::regex_match(clean_sql, match, insert_regex)) {
        stmt.type = "INSERT";
        stmt.table = match[1].str();
        
        std::string raw_vals = match[2].str();
        std::stringstream ss(raw_vals);
        std::string val;
        while (std::getline(ss, val, ',')) {
            val = trim(val);
            if ((val.front() == '\'' && val.back() == '\'') || 
                (val.front() == '"' && val.back() == '"')) {
                val = val.substr(1, val.length() - 2);
            }
            stmt.values.push_back(val);
        }
        return stmt;
    }

    // 2. Parse DELETE
    std::regex delete_regex(R"(^DELETE\s+FROM\s+(\w+)(?:\s+WHERE\s+(.*))?$)", std::regex_constants::icase);
    if (std::regex_match(clean_sql, match, delete_regex)) {
        stmt.type = "DELETE";
        stmt.table = match[1].str();
        if (match[2].matched) {
            stmt.where = parse_predicate(trim(match[2].str()));
        }
        return stmt;
    }

    // 3. Parse SELECT
    // SELECT <columns> FROM <t1> [JOIN <t2> ON <cond>] [WHERE <cond>]
    std::regex select_regex(
        R"(^SELECT\s+(.+?)\s+FROM\s+(\w+)(?:\s+JOIN\s+(\w+)\s+ON\s+(.+?))?(?:\s+WHERE\s+(.+?))?$)", 
        std::regex_constants::icase
    );
    if (std::regex_match(clean_sql, match, select_regex)) {
        stmt.type = "SELECT";
        stmt.table = match[2].str();
        
        std::string cols_str = match[1].str();
        std::stringstream ss(cols_str);
        std::string col;
        while (std::getline(ss, col, ',')) {
            stmt.columns.push_back(trim(col));
        }

        if (match[3].matched && match[4].matched) {
            std::string join_table = match[3].str();
            std::string on_cond = match[4].str();
            
            size_t eq_pos = on_cond.find('=');
            if (eq_pos != std::string::npos) {
                std::string left_col = trim(on_cond.substr(0, eq_pos));
                std::string right_col = trim(on_cond.substr(eq_pos + 1));
                stmt.join = JoinClause{join_table, left_col, right_col};
            }
        }

        if (match[5].matched) {
            stmt.where = parse_predicate(trim(match[5].str()));
        }
        return stmt;
    }

    throw std::runtime_error("Unsupported or malformed SQL statement: " + sql);
}
