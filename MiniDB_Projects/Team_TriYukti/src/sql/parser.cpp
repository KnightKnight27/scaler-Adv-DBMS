#include "sql/parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace minidb {

static std::string trim(const std::string &s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(*start)) {
        start++;
    }
    auto end = s.end();
    do {
        end--;
    } while (std::distance(start, end) > 0 && std::isspace(*end));
    return std::string(start, end + 1);
}

static std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::toupper(c); });
    return s;
}

static std::vector<std::string> split(const std::string &s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(trim(token));
    }
    return tokens;
}

ParsedStatement Parser::Parse(const std::string &sql) {
    ParsedStatement stmt;
    stmt.type = StatementType::UNKNOWN;
    
    std::string query = trim(sql);
    if (query.empty()) {
        stmt.type = StatementType::EMPTY;
        return stmt;
    }
    
    std::istringstream iss(query);
    std::string first_word;
    iss >> first_word;
    first_word = to_upper(first_word);
    
    if (first_word == "EXPLAIN") {
        std::string rest;
        std::getline(iss, rest);
        stmt = Parse(rest);
        if (stmt.type != StatementType::UNKNOWN && stmt.type != StatementType::EMPTY) {
            // Keep the real type but somehow mark as explain? We will just use the original stmt.
            // Wait, explain needs its own type or flag. Let's just return the stmt and let the caller check "EXPLAIN".
            // Since our parser is static, it doesn't return "is_explain".
            // Let's modify ParsedStatement. Wait, easier: EXPLAIN is handled by caller intercepting the prefix.
        }
        stmt.type = StatementType::EXPLAIN; // Simple override for now
        return stmt;
    }
    
    if (first_word == "BEGIN") { stmt.type = StatementType::BEGIN; return stmt; }
    if (first_word == "COMMIT") { stmt.type = StatementType::COMMIT; return stmt; }
    if (first_word == "ROLLBACK") { stmt.type = StatementType::ROLLBACK; return stmt; }
    
    if (first_word == "SHOW") {
        std::string second_word;
        iss >> second_word;
        second_word = to_upper(second_word);
        if (second_word == "LOCKS") stmt.type = StatementType::SHOW_LOCKS;
        if (second_word == "TRANSACTIONS") stmt.type = StatementType::SHOW_TRANSACTIONS;
        if (second_word == "TABLES") stmt.type = StatementType::SHOW_TABLES;
        return stmt;
    }
    
    if (first_word == "CREATE") {
        std::string second;
        iss >> second;
        if (to_upper(second) == "TABLE") {
            stmt.type = StatementType::CREATE_TABLE;
            iss >> stmt.table_name;
            
            // Very naive parsing of schema: (id int, name varchar)
            size_t start = query.find('(');
            size_t end = query.rfind(')');
            if (start != std::string::npos && end != std::string::npos && start < end) {
                std::string cols = query.substr(start + 1, end - start - 1);
                auto parts = split(cols, ',');
                for (auto &p : parts) {
                    auto words = split(p, ' ');
                    if (words.size() >= 2) {
                        stmt.columns_with_type.push_back({words[0], words[1]});
                    }
                }
            }
        }
        return stmt;
    }
    
    if (first_word == "INSERT") {
        std::string second;
        iss >> second; // INTO
        iss >> stmt.table_name;
        
        std::string values_kwd;
        iss >> values_kwd; // VALUES
        
        size_t start = query.find('(');
        size_t end = query.rfind(')');
        if (start != std::string::npos && end != std::string::npos && start < end) {
            std::string vals = query.substr(start + 1, end - start - 1);
            stmt.values = split(vals, ',');
        }
        stmt.type = StatementType::INSERT;
        return stmt;
    }
    
    if (first_word == "DELETE") {
        std::string from_kwd;
        iss >> from_kwd; // FROM
        iss >> stmt.table_name;
        
        std::string where_kwd;
        iss >> where_kwd;
        if (to_upper(where_kwd) == "WHERE") {
            stmt.has_where = true;
            iss >> stmt.where_column;
            iss >> stmt.where_op;
            iss >> stmt.where_value;
        }
        stmt.type = StatementType::DELETE;
        return stmt;
    }
    
    if (first_word == "SELECT") {
        stmt.type = StatementType::SELECT;
        
        // Parse columns
        std::string cols_str;
        std::string token;
        while (iss >> token && to_upper(token) != "FROM") {
            cols_str += token + " ";
        }
        cols_str = trim(cols_str);
        if (!cols_str.empty()) {
            if (cols_str.back() == ',') cols_str.pop_back(); // crude
            stmt.columns = split(cols_str, ',');
        }
        
        // Parse table
        iss >> stmt.table_name;
        
        // Parse JOIN or WHERE
        while (iss >> token) {
            std::string u_token = to_upper(token);
            if (u_token == "JOIN") {
                stmt.has_join = true;
                iss >> stmt.join_table;
                std::string on_kwd;
                iss >> on_kwd; // ON
                iss >> stmt.join_cond_left;
                std::string eq;
                iss >> eq; // =
                iss >> stmt.join_cond_right;
            } else if (u_token == "WHERE") {
                stmt.has_where = true;
                iss >> stmt.where_column;
                iss >> stmt.where_op;
                iss >> stmt.where_value;
            }
        }
    }
    
    return stmt;
}

} // namespace minidb
