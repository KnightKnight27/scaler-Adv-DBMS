#include "parser/sql_parser.h"

#include <cctype>
#include <sstream>
#include <stdexcept>

namespace minidb {

namespace {

std::string Trim(const std::string& s) {
    std::size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    std::size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string Upper(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::vector<std::string> Split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        item = Trim(item);
        if (!item.empty()) parts.push_back(item);
    }
    return parts;
}

Value ParseLiteral(const std::string& token) {
    if (Upper(token) == "NULL") return Value::Null();
    if (!token.empty() && token.front() == '\'' && token.back() == '\'') {
        return Value::Str(token.substr(1, token.size() - 2));
    }
    try {
        return Value::Int(std::stoll(token));
    } catch (...) {
        return Value::Str(token);
    }
}

ValueType ParseType(const std::string& token) {
    if (Upper(token) == "INT") return ValueType::INT;
    return ValueType::STRING;
}

}  // namespace

ParsedStatement SqlParser::Parse(const std::string& sql) const {
    std::string q = Trim(sql);
    while (!q.empty() && q.back() == ';') q.pop_back();
    q = Trim(q);
    std::string upper = Upper(q);

    ParsedStatement stmt;
    if (upper == "BEGIN" || upper == "BEGIN TRANSACTION") {
        stmt.type = StmtType::BEGIN_TXN;
        return stmt;
    }
    if (upper == "COMMIT" || upper == "COMMIT TRANSACTION") {
        stmt.type = StmtType::COMMIT;
        return stmt;
    }
    if (upper == "ROLLBACK") {
        stmt.type = StmtType::ROLLBACK;
        return stmt;
    }
    if (upper == "CHECKPOINT") {
        stmt.type = StmtType::CHECKPOINT;
        return stmt;
    }
    if (upper == "CRASH") {
        stmt.type = StmtType::CRASH;
        return stmt;
    }
    if (upper == "SET EXEC_MODE BATCH") {
        stmt.type = StmtType::USE_BATCH;
        return stmt;
    }
    if (upper == "SET EXEC_MODE ROW") {
        stmt.type = StmtType::USE_ROW;
        return stmt;
    }

    if (upper.rfind("CREATE TABLE", 0) == 0) {
        stmt.type = StmtType::CREATE_TABLE;
        auto lparen = q.find('(');
        auto rparen = q.rfind(')');
        if (lparen == std::string::npos || rparen == std::string::npos) {
            throw std::runtime_error("Invalid CREATE TABLE syntax");
        }
        std::string table_part = Trim(q.substr(12, lparen - 12));
        stmt.create_table.table = table_part;
        auto defs = Split(q.substr(lparen + 1, rparen - lparen - 1), ',');
        for (const auto& def : defs) {
            auto tokens = Split(def, ' ');
            if (tokens.size() < 2) continue;
            ColumnDef col;
            col.name = tokens[0];
            col.type = ParseType(tokens[1]);
            for (std::size_t i = 2; i < tokens.size(); ++i) {
                std::string t = Upper(tokens[i]);
                if (t == "PRIMARY" && i + 1 < tokens.size() && Upper(tokens[i + 1]) == "KEY") {
                    col.primary_key = true;
                }
                if (t == "INDEX") col.indexed = true;
            }
            stmt.create_table.columns.push_back(col);
        }
        return stmt;
    }

    if (upper.rfind("INSERT INTO", 0) == 0) {
        stmt.type = StmtType::INSERT;
        auto values_pos = Upper(q).find(" VALUES ");
        if (values_pos == std::string::npos) {
            throw std::runtime_error("INSERT requires VALUES clause");
        }
        std::string head = q.substr(11, values_pos - 11);
        std::string values_part = q.substr(values_pos + 8);
        auto lparen = head.find('(');
        if (lparen != std::string::npos) {
            stmt.insert.table = Trim(head.substr(0, lparen));
            auto rparen = head.find(')');
            stmt.insert.columns = Split(head.substr(lparen + 1, rparen - lparen - 1), ',');
        } else {
            stmt.insert.table = Trim(head);
        }
        lparen = values_part.find('(');
        auto rparen = values_part.rfind(')');
        auto vals = Split(values_part.substr(lparen + 1, rparen - lparen - 1), ',');
        for (const auto& v : vals) stmt.insert.values.push_back(ParseLiteral(v));
        return stmt;
    }

    if (upper.rfind("DELETE FROM", 0) == 0) {
        stmt.type = StmtType::DELETE_STMT;
        auto where_pos = Upper(q).find(" WHERE ");
        if (where_pos == std::string::npos) {
            stmt.delete_stmt.table = Trim(q.substr(11));
            return stmt;
        }
        stmt.delete_stmt.table = Trim(q.substr(11, where_pos - 11));
        auto pred = Trim(q.substr(where_pos + 7));
        auto parts = Split(pred, ' ');
        if (parts.size() >= 3) {
            stmt.delete_stmt.predicates.push_back(
                Predicate{parts[0], ParseCompareOp(parts[1]), ParseLiteral(parts[2])});
        }
        return stmt;
    }

    if (upper.rfind("SELECT", 0) == 0) {
        stmt.type = StmtType::SELECT;
        auto from_pos = Upper(q).find(" FROM ");
        if (from_pos == std::string::npos) {
            throw std::runtime_error("SELECT requires FROM");
        }
        std::string cols = Trim(q.substr(6, from_pos - 6));
        if (Upper(cols) != "*") {
            for (const auto& part : Split(cols, ',')) {
                std::string col = Trim(part);
                std::string up = Upper(col);
                if (up.rfind("COUNT(", 0) == 0) {
                    auto lparen = col.find('(');
                    auto rparen = col.find(')');
                    if (lparen == std::string::npos || rparen == std::string::npos) {
                        throw std::runtime_error("Invalid COUNT expression");
                    }
                    AggregateExpr agg;
                    agg.func = AggFunc::COUNT;
                    std::string arg = Trim(col.substr(lparen + 1, rparen - lparen - 1));
                    if (Upper(arg) != "*") {
                        agg.column = arg;
                    }
                    agg.alias = col;
                    stmt.select.aggregates.push_back(agg);
                } else {
                    stmt.select.columns.push_back(col);
                }
            }
        }
        std::string rest = q.substr(from_pos + 6);
        std::string upper_rest = Upper(rest);
        auto where_pos = upper_rest.find(" WHERE ");
        auto join_pos = upper_rest.find(" JOIN ");
        auto group_pos = upper_rest.find(" GROUP BY ");
        std::size_t table_end = rest.size();
        if (where_pos != std::string::npos) table_end = std::min(table_end, where_pos);
        if (join_pos != std::string::npos) table_end = std::min(table_end, join_pos);
        if (group_pos != std::string::npos) table_end = std::min(table_end, group_pos);
        stmt.select.tables = Split(Trim(rest.substr(0, table_end)), ',');
        if (join_pos != std::string::npos) {
            auto on_pos = upper_rest.find(" ON ", join_pos);
            if (on_pos == std::string::npos) {
                throw std::runtime_error("JOIN requires ON clause");
            }
            auto join_clause = Trim(rest.substr(join_pos + 6, on_pos - (join_pos + 6)));
            auto on_clause = Trim(rest.substr(on_pos + 4));
            auto eq = on_clause.find('=');
            JoinSpec js;
            js.left_table = stmt.select.tables[0];
            js.right_table = join_clause;
            js.left_col = Trim(on_clause.substr(0, eq));
            auto dot = js.left_col.find('.');
            if (dot != std::string::npos) js.left_col = js.left_col.substr(dot + 1);
            js.right_col = Trim(on_clause.substr(eq + 1));
            dot = js.right_col.find('.');
            if (dot != std::string::npos) js.right_col = js.right_col.substr(dot + 1);
            stmt.select.joins.push_back(js);
            if (!stmt.select.tables.empty()) {
                stmt.select.tables.push_back(join_clause);
            }
        }
        if (where_pos != std::string::npos) {
            std::size_t pred_end = rest.size();
            if (group_pos != std::string::npos) pred_end = group_pos;
            auto pred = Trim(rest.substr(where_pos + 7, pred_end - where_pos - 7));
            auto and_pos = Upper(pred).find(" AND ");
            if (and_pos != std::string::npos) pred = Trim(pred.substr(0, and_pos));
            auto parts = Split(pred, ' ');
            if (parts.size() >= 3) {
                std::string col = parts[0];
                auto dot = col.find('.');
                if (dot != std::string::npos) col = col.substr(dot + 1);
                stmt.select.predicates.push_back(
                    Predicate{col, ParseCompareOp(parts[1]), ParseLiteral(parts[2])});
            }
        }
        if (group_pos != std::string::npos) {
            stmt.select.group_by = Split(Trim(rest.substr(group_pos + 10)), ',');
        }
        return stmt;
    }

    throw std::runtime_error("Unsupported SQL: " + q);
}

}  // namespace minidb
