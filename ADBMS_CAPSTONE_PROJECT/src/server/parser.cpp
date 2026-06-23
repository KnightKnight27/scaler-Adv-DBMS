#include "server/parser.h"
#include <sstream>
#include <algorithm>

namespace minidb {

SQLStatement Parser::Parse(const std::string& query) {
    SQLStatement stmt;
    std::istringstream iss(query);
    std::vector<std::string> tokens;
    std::string token;
    
    while (iss >> token) {
        // Convert to uppercase for easy comparison
        std::string upper_token = token;
        std::transform(upper_token.begin(), upper_token.end(), upper_token.begin(), ::toupper);
        tokens.push_back(upper_token);
    }
    
    if (tokens.empty()) return stmt;
    
    if (tokens[0] == "SELECT") {
        stmt.type_ = StatementType::SELECT;
        // Expect: SELECT * FROM table_name [WHERE id = X]
        for (size_t i = 0; i < tokens.size(); i++) {
            if (tokens[i] == "FROM" && i + 1 < tokens.size()) {
                stmt.table_name_ = tokens[i + 1];
                if (stmt.table_name_ == "REPLICA_USERS") stmt.is_replica_ = true;
            } else if (tokens[i] == "WHERE" && i + 3 < tokens.size()) {
                if (tokens[i + 1] == "ID" && tokens[i + 2] == "=") {
                    stmt.has_where_ = true;
                    stmt.where_id_ = std::stoi(tokens[i + 3]);
                }
            }
        }
    } else if (tokens[0] == "INSERT") {
        // Expect: INSERT INTO table_name VALUES (id, name)
        stmt.type_ = StatementType::INSERT;
        for (size_t i = 0; i < tokens.size(); i++) {
            if (tokens[i] == "INTO" && i + 1 < tokens.size()) {
                stmt.table_name_ = tokens[i + 1];
            } else if (tokens[i] == "VALUES" && i + 1 < tokens.size()) {
                // Highly simplified parsing for (id, name)
                // Just assuming tokens after VALUES are id and name
                std::string vals = "";
                for (size_t j = i + 1; j < tokens.size(); j++) {
                    vals += tokens[j];
                }
                // Strip parentheses and split by comma
                vals.erase(std::remove(vals.begin(), vals.end(), '('), vals.end());
                vals.erase(std::remove(vals.begin(), vals.end(), ')'), vals.end());
                
                size_t comma = vals.find(',');
                if (comma != std::string::npos) {
                    stmt.insert_id_ = std::stoi(vals.substr(0, comma));
                    stmt.insert_name_ = vals.substr(comma + 1);
                }
            }
        }
    } else if (tokens[0] == "DELETE") {
        // Expect: DELETE FROM table_name WHERE id = X
        stmt.type_ = StatementType::DELETE;
        for (size_t i = 0; i < tokens.size(); i++) {
            if (tokens[i] == "FROM" && i + 1 < tokens.size()) {
                stmt.table_name_ = tokens[i + 1];
            } else if (tokens[i] == "WHERE" && i + 3 < tokens.size()) {
                if (tokens[i + 1] == "ID" && tokens[i + 2] == "=") {
                    stmt.has_where_ = true;
                    stmt.where_id_ = std::stoi(tokens[i + 3]);
                }
            }
        }
    }
    
    return stmt;
}

} // namespace minidb
