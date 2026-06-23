#include "query/parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>

// ─── toUpper ──────────────────────────────────────────────────────────────────

std::string Parser::toUpper(std::string s) const {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// ─── tokenize ─────────────────────────────────────────────────────────────────
// Split input by whitespace. Strips trailing commas from tokens (for VALUES parsing).

std::vector<std::string> Parser::tokenize(const std::string& input) const {
    std::vector<std::string> tokens;
    std::istringstream ss(input);
    std::string tok;
    while (ss >> tok) {
        // Strip parentheses and trailing commas for VALUES(...) syntax
        tok.erase(std::remove(tok.begin(), tok.end(), '('), tok.end());
        tok.erase(std::remove(tok.begin(), tok.end(), ')'), tok.end());
        if (!tok.empty() && tok.back() == ',') tok.pop_back();
        if (!tok.empty()) tokens.push_back(tok);
    }
    return tokens;
}

// ─── parseInt ─────────────────────────────────────────────────────────────────

std::optional<int32_t> Parser::parseInt(const std::string& s) const {
    try {
        size_t pos;
        long val = std::stol(s, &pos);
        if (pos != s.size()) return std::nullopt;
        return static_cast<int32_t>(val);
    } catch (...) {
        return std::nullopt;
    }
}

// ─── parse ────────────────────────────────────────────────────────────────────

ParsedQuery Parser::parse(const std::string& input) const {
    ParsedQuery q;
    auto tokens = tokenize(input);

    if (tokens.empty()) {
        q.error_msg = "Empty command";
        return q;
    }

    std::string cmd = toUpper(tokens[0]);

    // ── BEGIN / COMMIT / ABORT ──
    if (cmd == "BEGIN") {
        q.type = CmdType::BEGIN;
        q.valid = true;
        return q;
    }
    if (cmd == "COMMIT") {
        q.type = CmdType::COMMIT;
        q.valid = true;
        return q;
    }
    if (cmd == "ABORT" || cmd == "ROLLBACK") {
        q.type = CmdType::ABORT;
        q.valid = true;
        return q;
    }

    // ── QUIT / EXIT ──
    if (cmd == "QUIT" || cmd == "EXIT") {
        q.type = CmdType::QUIT;
        q.valid = true;
        return q;
    }

    // ── HELP ──
    if (cmd == "HELP") {
        q.type = CmdType::HELP;
        q.valid = true;
        return q;
    }

    // ── SHOW TABLES ──
    if (cmd == "SHOW" && tokens.size() >= 2 && toUpper(tokens[1]) == "TABLES") {
        q.type = CmdType::SHOW_TABLES;
        q.valid = true;
        return q;
    }

    // ── CREATE TABLE <name> ──
    if (cmd == "CREATE") {
        if (tokens.size() < 3 || toUpper(tokens[1]) != "TABLE") {
            q.error_msg = "Usage: CREATE TABLE <name>";
            return q;
        }
        q.type   = CmdType::CREATE_TABLE;
        q.table1 = tokens[2];
        q.valid  = true;
        return q;
    }

    // ── INSERT INTO <table> VALUES (<key>, <value>) ──
    if (cmd == "INSERT") {
        // tokens: INSERT INTO <table> VALUES <key> <value...>
        if (tokens.size() < 5 || toUpper(tokens[1]) != "INTO" || toUpper(tokens[3]) != "VALUES") {
            q.error_msg = "Usage: INSERT INTO <table> VALUES (<key>, <value>)";
            return q;
        }
        q.table1 = tokens[2];
        auto key_opt = parseInt(tokens[4]);
        if (!key_opt) {
            q.error_msg = "Key must be an integer";
            return q;
        }
        q.key = *key_opt;
        // Value is everything after the key token
        if (tokens.size() >= 6) {
            q.value = tokens[5];
            for (size_t i = 6; i < tokens.size(); ++i) {
                q.value += " " + tokens[i];
            }
        } else {
            q.value = "";
        }
        q.type  = CmdType::INSERT;
        q.valid = true;
        return q;
    }

    // ── DELETE FROM <table> WHERE id = <key> ──
    if (cmd == "DELETE") {
        // tokens: DELETE FROM <table> WHERE id = <key>
        if (tokens.size() < 7 || toUpper(tokens[1]) != "FROM") {
            q.error_msg = "Usage: DELETE FROM <table> WHERE id = <key>";
            return q;
        }
        q.table1 = tokens[2];
        auto key_opt = parseInt(tokens[6]);
        if (!key_opt) {
            q.error_msg = "Key must be an integer";
            return q;
        }
        q.key   = *key_opt;
        q.type  = CmdType::DELETE_KEY;
        q.valid = true;
        return q;
    }

    // ── SELECT ──
    if (cmd == "SELECT") {
        // Minimum: SELECT * FROM <table>
        // Find FROM keyword
        size_t from_idx = std::string::npos;
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (toUpper(tokens[i]) == "FROM") {
                from_idx = i;
                break;
            }
        }

        if (from_idx == std::string::npos || from_idx + 1 >= tokens.size()) {
            q.error_msg = "Usage: SELECT * FROM <table> [WHERE id = <key>] [JOIN ...]";
            return q;
        }

        q.table1 = tokens[from_idx + 1];

        // Check for JOIN
        size_t join_idx = std::string::npos;
        for (size_t i = from_idx + 2; i < tokens.size(); ++i) {
            if (toUpper(tokens[i]) == "JOIN") {
                join_idx = i;
                break;
            }
        }

        if (join_idx != std::string::npos) {
            if (join_idx + 1 >= tokens.size()) {
                q.error_msg = "Usage: SELECT * FROM <t1> JOIN <t2> ON t1.id = t2.id";
                return q;
            }
            q.table2 = tokens[join_idx + 1];
            q.type   = CmdType::JOIN;
            q.valid  = true;
            return q;
        }

        // Check for WHERE
        size_t where_idx = std::string::npos;
        for (size_t i = from_idx + 2; i < tokens.size(); ++i) {
            if (toUpper(tokens[i]) == "WHERE") {
                where_idx = i;
                break;
            }
        }

        if (where_idx == std::string::npos) {
            // SELECT * FROM <table>
            q.type  = CmdType::SELECT_ALL;
            q.valid = true;
            return q;
        }

        // WHERE clause: expect "id <op> <key>"
        // tokens[where_idx+1] = "id"
        // tokens[where_idx+2] = operator
        // tokens[where_idx+3] = key
        if (where_idx + 3 >= tokens.size()) {
            q.error_msg = "Usage: SELECT * FROM <table> WHERE id = <key>";
            return q;
        }

        q.op = tokens[where_idx + 2];
        auto key_opt = parseInt(tokens[where_idx + 3]);
        if (!key_opt) {
            q.error_msg = "Key must be an integer";
            return q;
        }
        q.key = *key_opt;

        if (q.op == "=") {
            q.type = CmdType::SELECT_KEY;
        } else if (q.op == ">" || q.op == "<" || q.op == ">=" || q.op == "<=") {
            q.type = CmdType::SELECT_RANGE;
        } else {
            q.error_msg = "Unsupported operator: " + q.op + " (use =, >, <, >=, <=)";
            return q;
        }

        q.valid = true;
        return q;
    }

    q.error_msg = "Unknown command: " + tokens[0] + ". Type HELP for available commands.";
    return q;
}
