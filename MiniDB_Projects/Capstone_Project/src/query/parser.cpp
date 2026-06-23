#include "query/parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>

std::string Parser::toUpper(std::string s) const {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

std::vector<std::string> Parser::tokenize(const std::string& input) const {
    std::vector<std::string> tokens;
    std::istringstream ss(input);
    std::string tok;
    while (ss >> tok) {
        tok.erase(std::remove(tok.begin(), tok.end(), '('), tok.end());
        tok.erase(std::remove(tok.begin(), tok.end(), ')'), tok.end());
        if (!tok.empty() && tok.back() == ',') {
            tok.pop_back();
        }
        if (!tok.empty()) {
            tokens.push_back(tok);
        }
    }
    return tokens;
}

std::optional<int32_t> Parser::parseInt(const std::string& s) const {
    try {
        size_t pos;
        const long val = std::stol(s, &pos);
        if (pos != s.size()) {
            return std::nullopt;
        }
        return static_cast<int32_t>(val);
    } catch (...) {
        return std::nullopt;
    }
}

ParsedQuery Parser::parse(const std::string& input) const {
    ParsedQuery query;
    const auto tokens = tokenize(input);

    if (tokens.empty()) {
        query.error_msg = "Empty command";
        return query;
    }

    const std::string cmd = toUpper(tokens[0]);

    if (cmd == "BEGIN") {
        query.type = CmdType::BEGIN;
        query.valid = true;
        return query;
    }
    if (cmd == "COMMIT") {
        query.type = CmdType::COMMIT;
        query.valid = true;
        return query;
    }
    if (cmd == "ABORT" || cmd == "ROLLBACK") {
        query.type = CmdType::ABORT;
        query.valid = true;
        return query;
    }
    if (cmd == "QUIT" || cmd == "EXIT") {
        query.type = CmdType::QUIT;
        query.valid = true;
        return query;
    }
    if (cmd == "HELP") {
        query.type = CmdType::HELP;
        query.valid = true;
        return query;
    }
    if (cmd == "SHOW" && tokens.size() >= 2 && toUpper(tokens[1]) == "TABLES") {
        query.type = CmdType::SHOW_TABLES;
        query.valid = true;
        return query;
    }

    if (cmd == "CREATE") {
        if (tokens.size() < 3 || toUpper(tokens[1]) != "TABLE") {
            query.error_msg = "Usage: CREATE TABLE <name>";
            return query;
        }
        query.type = CmdType::CREATE_TABLE;
        query.table1 = tokens[2];
        query.valid = true;
        return query;
    }

    if (cmd == "INSERT") {
        if (tokens.size() < 5 || toUpper(tokens[1]) != "INTO" || toUpper(tokens[3]) != "VALUES") {
            query.error_msg = "Usage: INSERT INTO <table> VALUES (<key>, <value>)";
            return query;
        }
        query.table1 = tokens[2];
        const auto key_opt = parseInt(tokens[4]);
        if (!key_opt) {
            query.error_msg = "Key must be an integer";
            return query;
        }
        query.key = *key_opt;
        
        if (tokens.size() >= 6) {
            query.value = tokens[5];
            for (size_t i = 6; i < tokens.size(); ++i) {
                query.value += " " + tokens[i];
            }
        } else {
            query.value = "";
        }
        query.type = CmdType::INSERT;
        query.valid = true;
        return query;
    }

    if (cmd == "DELETE") {
        if (tokens.size() < 7 || toUpper(tokens[1]) != "FROM") {
            query.error_msg = "Usage: DELETE FROM <table> WHERE id = <key>";
            return query;
        }
        query.table1 = tokens[2];
        const auto key_opt = parseInt(tokens[6]);
        if (!key_opt) {
            query.error_msg = "Key must be an integer";
            return query;
        }
        query.key = *key_opt;
        query.type = CmdType::DELETE_KEY;
        query.valid = true;
        return query;
    }

    if (cmd == "SELECT") {
        size_t from_idx = std::string::npos;
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (toUpper(tokens[i]) == "FROM") {
                from_idx = i;
                break;
            }
        }

        if (from_idx == std::string::npos || from_idx + 1 >= tokens.size()) {
            query.error_msg = "Usage: SELECT * FROM <table> [WHERE id = <key>] [JOIN ...]";
            return query;
        }

        query.table1 = tokens[from_idx + 1];

        size_t join_idx = std::string::npos;
        for (size_t i = from_idx + 2; i < tokens.size(); ++i) {
            if (toUpper(tokens[i]) == "JOIN") {
                join_idx = i;
                break;
            }
        }

        if (join_idx != std::string::npos) {
            if (join_idx + 1 >= tokens.size()) {
                query.error_msg = "Usage: SELECT * FROM <t1> JOIN <t2> ON t1.id = t2.id";
                return query;
            }
            query.table2 = tokens[join_idx + 1];
            query.type = CmdType::JOIN;
            query.valid = true;
            return query;
        }

        size_t where_idx = std::string::npos;
        for (size_t i = from_idx + 2; i < tokens.size(); ++i) {
            if (toUpper(tokens[i]) == "WHERE") {
                where_idx = i;
                break;
            }
        }

        if (where_idx == std::string::npos) {
            query.type = CmdType::SELECT_ALL;
            query.valid = true;
            return query;
        }

        if (where_idx + 3 >= tokens.size()) {
            query.error_msg = "Usage: SELECT * FROM <table> WHERE id = <key>";
            return query;
        }

        query.op = tokens[where_idx + 2];
        const auto key_opt = parseInt(tokens[where_idx + 3]);
        if (!key_opt) {
            query.error_msg = "Key must be an integer";
            return query;
        }
        query.key = *key_opt;

        if (query.op == "=") {
            query.type = CmdType::SELECT_KEY;
        } else if (query.op == ">" || query.op == "<" || query.op == ">=" || query.op == "<=") {
            query.type = CmdType::SELECT_RANGE;
        } else {
            query.error_msg = "Unsupported operator: " + query.op + " (use =, >, <, >=, <=)";
            return query;
        }

        query.valid = true;
        return query;
    }

    query.error_msg = "Unknown command: " + tokens[0] + ". Type HELP for available commands.";
    return query;
}
