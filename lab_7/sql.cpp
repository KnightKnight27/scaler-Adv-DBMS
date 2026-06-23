//
// sql.cpp
// ---------------------------------------------------------------------------
// Implementation of the minimal SQL SELECT parser + executor.
// ---------------------------------------------------------------------------

#include "sql.h"
#include "shunting_yard.h"
#include <iostream>
#include <stdexcept>
#include <set>

namespace {

// A tiny cursor over the token stream for the SELECT/FROM/WHERE skeleton.
class Parser {
public:
    explicit Parser(const std::vector<Token> &toks) : tokens(toks) {}

    SelectStatement parse() {
        SelectStatement stmt;

        expect(TokenType::SELECT);

        // ---- projection list: '*'  OR  ident (',' ident)* ----
        if (cur().type == TokenType::STAR) {
            stmt.selectAll = true;
            advance();
        } else {
            stmt.columns.push_back(expect(TokenType::IDENTIFIER).text);
            while (cur().type == TokenType::COMMA) {
                advance();
                stmt.columns.push_back(expect(TokenType::IDENTIFIER).text);
            }
        }

        expect(TokenType::FROM);
        stmt.tableName = expect(TokenType::IDENTIFIER).text;

        // ---- optional WHERE ----
        if (cur().type == TokenType::WHERE) {
            advance();
            stmt.hasWhere = true;
            // Capture every remaining token up to END as the raw infix
            // predicate; the shunting-yard engine validates it later.
            while (cur().type != TokenType::END) {
                stmt.whereInfix.push_back(cur());
                advance();
            }
        }

        expect(TokenType::END);
        return stmt;
    }

private:
    const std::vector<Token> &tokens;
    size_t pos = 0;

    const Token &cur() const { return tokens[pos]; }
    void advance() { if (pos + 1 < tokens.size()) ++pos; }

    const Token &expect(TokenType t) {
        if (cur().type != t) {
            throw std::runtime_error(
                std::string("Parse error: expected ") + tokenTypeName(t) +
                " but found " + tokenTypeName(cur().type) +
                " ('" + cur().text + "')");
        }
        const Token &tok = tokens[pos];
        advance();
        return tok;
    }
};

} // namespace

SelectStatement parseSelect(const std::vector<Token> &tokens) {
    Parser p(tokens);
    return p.parse();
}

Table executeSelect(const SelectStatement &stmt, const Catalog &catalog) {
    auto it = catalog.find(stmt.tableName);
    if (it == catalog.end()) {
        throw std::runtime_error("Unknown table: " + stmt.tableName);
    }
    const Table &source = it->second;

    // Compile WHERE -> RPN exactly once (not per row).
    std::vector<Token> rpn;
    if (stmt.hasWhere) {
        rpn = toRPN(stmt.whereInfix);
    }

    Table result;
    for (const Row &row : source) {
        if (stmt.hasWhere && !evalPredicate(rpn, row)) {
            continue;                       // row filtered out
        }

        // Projection.
        if (stmt.selectAll) {
            result.push_back(row);
        } else {
            Row projected;
            for (const std::string &col : stmt.columns) {
                auto cit = row.find(col);
                if (cit == row.end()) {
                    throw std::runtime_error("Unknown column in projection: " + col);
                }
                projected[col] = cit->second;
            }
            result.push_back(projected);
        }
    }
    return result;
}

Table runQuery(const std::string &sql, const Catalog &catalog) {
    Lexer lexer(sql);
    std::vector<Token> tokens = lexer.tokenize();
    SelectStatement stmt = parseSelect(tokens);
    return executeSelect(stmt, catalog);
}

void printResult(const Table &result) {
    if (result.empty()) {
        std::cout << "  (0 rows)\n";
        return;
    }
    // Collect the union of column names, in deterministic (map) order.
    std::set<std::string> cols;
    for (const Row &r : result)
        for (const auto &kv : r) cols.insert(kv.first);

    // header
    std::cout << "  ";
    for (const std::string &c : cols) std::cout << c << "\t";
    std::cout << "\n";

    // rows
    for (const Row &r : result) {
        std::cout << "  ";
        for (const std::string &c : cols) {
            auto it = r.find(c);
            std::cout << (it == r.end() ? std::string("NULL") : valueToString(it->second)) << "\t";
        }
        std::cout << "\n";
    }
    std::cout << "  (" << result.size() << " row" << (result.size() == 1 ? "" : "s") << ")\n";
}
