#include "query/parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace minidb {

static std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::toupper(c); });
    return s;
}

Statement Parser::Parse(const std::string& line) {
    Statement s;
    std::istringstream ss(line);
    std::string cmd;
    if (!(ss >> cmd)) return s;
    cmd = upper(cmd);

    if (cmd == "INSERT") {
        s.type = StmtType::INSERT;
        if (!(ss >> s.id >> s.value)) s.type = StmtType::INVALID;
    } else if (cmd == "SELECT") {
        std::string arg;
        if (!(ss >> arg)) { s.type = StmtType::INVALID; return s; }
        std::string upper_arg = upper(arg);
        if (upper_arg == "JOIN") {
            s.type = StmtType::SELECT_JOIN;
            return s;
        }
        s.type = StmtType::SELECT;
        if (arg == "*") {
            s.id = -1;
            std::string next_word;
            if (ss >> next_word) {
                next_word = upper(next_word);
                if (next_word == "WHERE") {
                    std::string col, op;
                    int32_t val;
                    if (ss >> col >> op >> val) {
                        col = upper(col);
                        if (col == "ID") s.where_col = ColType::ID;
                        else if (col == "VALUE") s.where_col = ColType::VALUE;
                        else { s.type = StmtType::INVALID; return s; }

                        if (op == "=") s.where_op = OpType::EQUAL;
                        else if (op == ">") s.where_op = OpType::GREATER;
                        else { s.type = StmtType::INVALID; return s; }

                        s.where_val = val;
                    } else {
                        s.type = StmtType::INVALID;
                    }
                } else if (next_word == "JOIN") {
                    s.type = StmtType::SELECT_JOIN;
                } else {
                    s.type = StmtType::INVALID;
                }
            }
        } else {
            try { s.id = std::stoi(arg); } catch (...) { s.type = StmtType::INVALID; }
        }
    } else if (cmd == "DELETE") {
        s.type = StmtType::DELETE;
        if (!(ss >> s.id)) s.type = StmtType::INVALID;
    } else if (cmd == "SHOW")   { s.type = StmtType::SHOW;   s.id = -1; }
    else if (cmd == "BEGIN")    { s.type = StmtType::BEGIN;  }
    else if (cmd == "COMMIT")   { s.type = StmtType::COMMIT; }
    else if (cmd == "ABORT")    { s.type = StmtType::ABORT;  }
    return s;
}

} // namespace minidb
