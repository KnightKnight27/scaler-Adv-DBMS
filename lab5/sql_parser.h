#pragma once
/**
 * Lab 5 — Minimal SQL SELECT Parser
 *
 * Parses and executes SQL SELECT statements over an in-memory vector<Row>.
 * Supports:
 *   - SELECT col1, col2, * FROM table
 *   - WHERE clause with AND, OR, comparisons
 *   - ORDER BY col [ASC|DESC]
 *   - LIMIT n
 *
 * Uses the Shunting-Yard evaluator for WHERE clause evaluation.
 */

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <functional>

#include "shunting_yard.h"

// ─────────────────────────────────────────────────
// Row: A record in our in-memory table
// ─────────────────────────────────────────────────
using Row = std::unordered_map<std::string, Value>;

// ─────────────────────────────────────────────────
// Table: Named collection of rows with a schema
// ─────────────────────────────────────────────────
struct Table {
    std::string              name;
    std::vector<std::string> columns;
    std::vector<Row>         rows;

    void add_row(const std::vector<Value>& values) {
        if (values.size() != columns.size()) {
            throw std::runtime_error("Row size mismatch");
        }
        Row row;
        for (size_t i = 0; i < columns.size(); i++) {
            row[columns[i]] = values[i];
        }
        rows.push_back(row);
    }
};

// ─────────────────────────────────────────────────
// SQL Token types
// ─────────────────────────────────────────────────
enum class SQLTokenType {
    SELECT, FROM, WHERE, ORDER, BY, ASC, DESC, LIMIT, AND, OR, NOT,
    STAR, COMMA, DOT,
    EQ, NEQ, LT, GT, LTE, GTE,
    LPAREN, RPAREN,
    IDENTIFIER, NUMBER, STRING,
    END
};

struct SQLToken {
    SQLTokenType type;
    std::string  value;

    SQLToken(SQLTokenType t, const std::string& v = "") : type(t), value(v) {}
};

// ─────────────────────────────────────────────────
// SQL Tokenizer
// ─────────────────────────────────────────────────
class SQLTokenizer {
public:
    static std::vector<SQLToken> tokenize(const std::string& sql) {
        std::vector<SQLToken> tokens;
        size_t i = 0;

        while (i < sql.size()) {
            if (std::isspace(sql[i])) { i++; continue; }

            // Numbers
            if (std::isdigit(sql[i]) || (sql[i] == '.' && i+1 < sql.size() && std::isdigit(sql[i+1]))) {
                size_t start = i;
                while (i < sql.size() && (std::isdigit(sql[i]) || sql[i] == '.')) i++;
                tokens.emplace_back(SQLTokenType::NUMBER, sql.substr(start, i - start));
                continue;
            }

            // String literals
            if (sql[i] == '\'' || sql[i] == '"') {
                char quote = sql[i++];
                size_t start = i;
                while (i < sql.size() && sql[i] != quote) i++;
                tokens.emplace_back(SQLTokenType::STRING, sql.substr(start, i - start));
                if (i < sql.size()) i++;
                continue;
            }

            // Keywords and identifiers
            if (std::isalpha(sql[i]) || sql[i] == '_') {
                size_t start = i;
                while (i < sql.size() && (std::isalnum(sql[i]) || sql[i] == '_')) i++;
                std::string word = sql.substr(start, i - start);
                std::string upper = word;
                std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

                if (upper == "SELECT")      tokens.emplace_back(SQLTokenType::SELECT);
                else if (upper == "FROM")   tokens.emplace_back(SQLTokenType::FROM);
                else if (upper == "WHERE")  tokens.emplace_back(SQLTokenType::WHERE);
                else if (upper == "ORDER")  tokens.emplace_back(SQLTokenType::ORDER);
                else if (upper == "BY")     tokens.emplace_back(SQLTokenType::BY);
                else if (upper == "ASC")    tokens.emplace_back(SQLTokenType::ASC);
                else if (upper == "DESC")   tokens.emplace_back(SQLTokenType::DESC);
                else if (upper == "LIMIT")  tokens.emplace_back(SQLTokenType::LIMIT);
                else if (upper == "AND")    tokens.emplace_back(SQLTokenType::AND);
                else if (upper == "OR")     tokens.emplace_back(SQLTokenType::OR);
                else if (upper == "NOT")    tokens.emplace_back(SQLTokenType::NOT);
                else                        tokens.emplace_back(SQLTokenType::IDENTIFIER, word);
                continue;
            }

            // Two-character operators
            if (i + 1 < sql.size()) {
                std::string two = sql.substr(i, 2);
                if (two == "!=") { tokens.emplace_back(SQLTokenType::NEQ); i += 2; continue; }
                if (two == "<>") { tokens.emplace_back(SQLTokenType::NEQ); i += 2; continue; }
                if (two == "<=") { tokens.emplace_back(SQLTokenType::LTE); i += 2; continue; }
                if (two == ">=") { tokens.emplace_back(SQLTokenType::GTE); i += 2; continue; }
            }

            // Single character
            switch (sql[i]) {
                case '*': tokens.emplace_back(SQLTokenType::STAR); break;
                case ',': tokens.emplace_back(SQLTokenType::COMMA); break;
                case '.': tokens.emplace_back(SQLTokenType::DOT); break;
                case '=': tokens.emplace_back(SQLTokenType::EQ); break;
                case '<': tokens.emplace_back(SQLTokenType::LT); break;
                case '>': tokens.emplace_back(SQLTokenType::GT); break;
                case '(': tokens.emplace_back(SQLTokenType::LPAREN); break;
                case ')': tokens.emplace_back(SQLTokenType::RPAREN); break;
                default:
                    throw std::runtime_error("SQL: Unexpected character: " + std::string(1, sql[i]));
            }
            i++;
        }
        tokens.emplace_back(SQLTokenType::END);
        return tokens;
    }
};

// ─────────────────────────────────────────────────
// Parsed SQL SELECT statement
// ─────────────────────────────────────────────────
struct SelectStatement {
    std::vector<std::string> columns;       // selected columns (* = all)
    std::string              table_name;    // FROM table
    std::string              where_clause;  // WHERE expression (raw string)
    std::string              order_by;      // ORDER BY column
    bool                     order_desc = false;
    int                      limit = -1;    // LIMIT n (-1 = no limit)
    bool                     select_all = false;
};

// ─────────────────────────────────────────────────
// SQL Parser
// ─────────────────────────────────────────────────
class SQLParser {
private:
    std::vector<SQLToken> tokens_;
    size_t pos_;

    const SQLToken& current() const {
        return tokens_[pos_];
    }

    const SQLToken& advance() {
        return tokens_[pos_++];
    }

    bool match(SQLTokenType type) {
        if (current().type == type) { pos_++; return true; }
        return false;
    }

    void expect(SQLTokenType type, const std::string& context) {
        if (!match(type)) {
            throw std::runtime_error("SQL Parse Error: Expected " + context +
                                     " at position " + std::to_string(pos_));
        }
    }

public:
    SelectStatement parse(const std::string& sql) {
        tokens_ = SQLTokenizer::tokenize(sql);
        pos_ = 0;

        SelectStatement stmt;

        // SELECT
        expect(SQLTokenType::SELECT, "SELECT");

        // Column list
        if (current().type == SQLTokenType::STAR) {
            stmt.select_all = true;
            advance();
        } else {
            do {
                if (current().type != SQLTokenType::IDENTIFIER) {
                    throw std::runtime_error("Expected column name");
                }
                stmt.columns.push_back(current().value);
                advance();
            } while (match(SQLTokenType::COMMA));
        }

        // FROM
        expect(SQLTokenType::FROM, "FROM");
        if (current().type != SQLTokenType::IDENTIFIER) {
            throw std::runtime_error("Expected table name after FROM");
        }
        stmt.table_name = current().value;
        advance();

        // WHERE (optional)
        if (current().type == SQLTokenType::WHERE) {
            advance();
            // Collect tokens until ORDER, LIMIT, or END
            std::string where_str;
            while (current().type != SQLTokenType::ORDER &&
                   current().type != SQLTokenType::LIMIT &&
                   current().type != SQLTokenType::END) {
                const SQLToken& t = current();
                switch (t.type) {
                    case SQLTokenType::IDENTIFIER: where_str += t.value; break;
                    case SQLTokenType::NUMBER:     where_str += t.value; break;
                    case SQLTokenType::STRING:     where_str += "'" + t.value + "'"; break;
                    case SQLTokenType::AND:        where_str += " AND "; break;
                    case SQLTokenType::OR:         where_str += " OR "; break;
                    case SQLTokenType::NOT:        where_str += " NOT "; break;
                    case SQLTokenType::EQ:         where_str += " == "; break;
                    case SQLTokenType::NEQ:        where_str += " != "; break;
                    case SQLTokenType::LT:         where_str += " < "; break;
                    case SQLTokenType::GT:         where_str += " > "; break;
                    case SQLTokenType::LTE:        where_str += " <= "; break;
                    case SQLTokenType::GTE:        where_str += " >= "; break;
                    case SQLTokenType::LPAREN:     where_str += "("; break;
                    case SQLTokenType::RPAREN:     where_str += ")"; break;
                    default: where_str += " "; break;
                }
                advance();
            }
            stmt.where_clause = where_str;
        }

        // ORDER BY (optional)
        if (current().type == SQLTokenType::ORDER) {
            advance();
            expect(SQLTokenType::BY, "BY after ORDER");
            if (current().type != SQLTokenType::IDENTIFIER) {
                throw std::runtime_error("Expected column name after ORDER BY");
            }
            stmt.order_by = current().value;
            advance();
            if (current().type == SQLTokenType::DESC) {
                stmt.order_desc = true;
                advance();
            } else if (current().type == SQLTokenType::ASC) {
                advance();
            }
        }

        // LIMIT (optional)
        if (current().type == SQLTokenType::LIMIT) {
            advance();
            if (current().type != SQLTokenType::NUMBER) {
                throw std::runtime_error("Expected number after LIMIT");
            }
            stmt.limit = std::stoi(current().value);
            advance();
        }

        return stmt;
    }
};

// ─────────────────────────────────────────────────
// SQL Executor: Evaluates parsed SELECT over vector<Row>
// ─────────────────────────────────────────────────
class SQLExecutor {
private:
    std::unordered_map<std::string, Table*> tables_;

    bool evaluate_where(const Row& row, const std::string& where_clause) {
        if (where_clause.empty()) return true;

        // Build variable map from row
        std::unordered_map<std::string, Value> vars;
        for (const auto& [col, val] : row) {
            vars[col] = val;
        }

        Value result = ShuntingYard::eval(where_clause, vars);
        return value_to_bool(result);
    }

public:
    void register_table(Table& table) {
        tables_[table.name] = &table;
    }

    std::vector<Row> execute(const std::string& sql) {
        SQLParser parser;
        SelectStatement stmt = parser.parse(sql);

        // Find table
        auto it = tables_.find(stmt.table_name);
        if (it == tables_.end()) {
            throw std::runtime_error("Table not found: " + stmt.table_name);
        }
        Table& table = *(it->second);

        // Resolve columns
        std::vector<std::string> result_columns;
        if (stmt.select_all) {
            result_columns = table.columns;
        } else {
            result_columns = stmt.columns;
        }

        // Filter rows (WHERE)
        std::vector<Row> result;
        for (const auto& row : table.rows) {
            if (evaluate_where(row, stmt.where_clause)) {
                // Project selected columns
                Row projected;
                for (const auto& col : result_columns) {
                    auto cit = row.find(col);
                    if (cit != row.end()) {
                        projected[col] = cit->second;
                    }
                }
                result.push_back(projected);
            }
        }

        // ORDER BY
        if (!stmt.order_by.empty()) {
            std::sort(result.begin(), result.end(),
                [&stmt](const Row& a, const Row& b) {
                    auto ait = a.find(stmt.order_by);
                    auto bit = b.find(stmt.order_by);
                    if (ait == a.end() || bit == b.end()) return false;

                    double da = value_to_double(ait->second);
                    double db = value_to_double(bit->second);

                    if (stmt.order_desc) return da > db;
                    return da < db;
                });
        }

        // LIMIT
        if (stmt.limit >= 0 && static_cast<int>(result.size()) > stmt.limit) {
            result.resize(stmt.limit);
        }

        return result;
    }

    // Pretty print results
    void print_results(const std::string& sql) {
        SQLParser parser;
        SelectStatement stmt = parser.parse(sql);

        auto it = tables_.find(stmt.table_name);
        if (it == tables_.end()) throw std::runtime_error("Table not found: " + stmt.table_name);

        std::vector<std::string> cols;
        if (stmt.select_all) cols = it->second->columns;
        else cols = stmt.columns;

        auto rows = execute(sql);

        // Header
        std::cout << "\n  SQL: " << sql << std::endl;
        std::cout << "  " << std::string(cols.size() * 15 + 1, '-') << std::endl;
        std::cout << "  |";
        for (const auto& col : cols) {
            std::cout << std::setw(14) << col << "|";
        }
        std::cout << std::endl;
        std::cout << "  " << std::string(cols.size() * 15 + 1, '-') << std::endl;

        // Rows
        for (const auto& row : rows) {
            std::cout << "  |";
            for (const auto& col : cols) {
                auto cit = row.find(col);
                std::string val = (cit != row.end()) ? value_to_string(cit->second) : "NULL";
                std::cout << std::setw(14) << val << "|";
            }
            std::cout << std::endl;
        }
        std::cout << "  " << std::string(cols.size() * 15 + 1, '-') << std::endl;
        std::cout << "  " << rows.size() << " row(s)" << std::endl;
    }
};
