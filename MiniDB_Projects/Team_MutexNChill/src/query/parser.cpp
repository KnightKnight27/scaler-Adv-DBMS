#include "parser.h"
#include <stdexcept>

Parser::Parser(const std::vector<Token>& tokens) : tokens(tokens), pos(0) {}

Token& Parser::cur() {
    return const_cast<Token&>(tokens[pos]);
}

bool Parser::peek(TokenType t) {
    return tokens[pos].type == t;
}

Token& Parser::consume(TokenType expected) {
    if (tokens[pos].type != expected) {
        throw std::runtime_error("Parse error: unexpected token '" + tokens[pos].text + "'");
    }
    return const_cast<Token&>(tokens[pos++]);
}

ParseResult* Parser::parse() {
    if (peek(TokenType::SELECT)) return parseSelect();
    if (peek(TokenType::INSERT)) return parseInsert();
    if (peek(TokenType::DELETE)) return parseDelete();
    throw std::runtime_error("Expected SELECT, INSERT, or DELETE");
}

// SELECT col1, col2 FROM table [JOIN other ON t1.c = t2.c] [WHERE expr]
ParseResult* Parser::parseSelect() {
    consume(TokenType::SELECT);
    auto* stmt = new SelectStmt();

    // Column list: * or comma-separated identifiers (with optional table.col)
    if (peek(TokenType::STAR)) {
        consume(TokenType::STAR);
        stmt->columns.push_back("*");
    } else {
        // First column
        std::string col = consume(TokenType::IDENTIFIER).text;
        if (peek(TokenType::DOT)) {
            consume(TokenType::DOT);
            std::string c2 = consume(TokenType::IDENTIFIER).text;
            col = col + "." + c2;
        }
        stmt->columns.push_back(col);
        // More columns
        while (peek(TokenType::COMMA)) {
            consume(TokenType::COMMA);
            std::string c = consume(TokenType::IDENTIFIER).text;
            if (peek(TokenType::DOT)) {
                consume(TokenType::DOT);
                std::string c2 = consume(TokenType::IDENTIFIER).text;
                c = c + "." + c2;
            }
            stmt->columns.push_back(c);
        }
    }

    consume(TokenType::FROM);
    stmt->table = consume(TokenType::IDENTIFIER).text;

    // Optional JOIN
    if (peek(TokenType::JOIN)) {
        consume(TokenType::JOIN);
        stmt->join_table = consume(TokenType::IDENTIFIER).text;
        consume(TokenType::ON);
        // Parse:  table1.col = table2.col
        std::string t1 = consume(TokenType::IDENTIFIER).text;
        consume(TokenType::DOT);
        std::string c1 = consume(TokenType::IDENTIFIER).text;
        consume(TokenType::EQ);
        std::string t2 = consume(TokenType::IDENTIFIER).text;
        consume(TokenType::DOT);
        std::string c2 = consume(TokenType::IDENTIFIER).text;
        stmt->join_left_col  = t1 + "." + c1;
        stmt->join_right_col = t2 + "." + c2;
    }

    // Optional WHERE
    if (peek(TokenType::WHERE)) {
        consume(TokenType::WHERE);
        stmt->where = parseExpr();
    }

    auto* result = new ParseResult();
    result->type   = StmtType::SELECT;
    result->select = stmt;
    return result;
}

// INSERT INTO table VALUES (id, 'name', age, extra)
ParseResult* Parser::parseInsert() {
    consume(TokenType::INSERT);
    consume(TokenType::INTO);
    auto* stmt  = new InsertStmt();
    stmt->table = consume(TokenType::IDENTIFIER).text;
    consume(TokenType::VALUES);
    consume(TokenType::LPAREN);
    stmt->id    = std::stoi(consume(TokenType::NUMBER).text);
    consume(TokenType::COMMA);
    stmt->name  = consume(TokenType::STRING).text;
    consume(TokenType::COMMA);
    stmt->age   = std::stoi(consume(TokenType::NUMBER).text);
    consume(TokenType::COMMA);
    stmt->extra = std::stoi(consume(TokenType::NUMBER).text);
    consume(TokenType::RPAREN);

    auto* result = new ParseResult();
    result->type   = StmtType::INSERT;
    result->insert = stmt;
    return result;
}

// DELETE FROM table WHERE expr
ParseResult* Parser::parseDelete() {
    consume(TokenType::DELETE);
    consume(TokenType::FROM);
    auto* stmt  = new DeleteStmt();
    stmt->table = consume(TokenType::IDENTIFIER).text;
    if (peek(TokenType::WHERE)) {
        consume(TokenType::WHERE);
        stmt->where = parseExpr();
    }
    auto* result = new ParseResult();
    result->type = StmtType::DELETE;
    result->del  = stmt;
    return result;
}

// ---- Expression parsing ----
// Grammar: expr -> and_expr (OR and_expr)*
Expr* Parser::parseExpr() {
    Expr* left = parseAnd();
    while (peek(TokenType::OR)) {
        consume(TokenType::OR);
        Expr* right = parseAnd();
        left = new LogicExpr("OR", left, right);
    }
    return left;
}

// and_expr -> comparison (AND comparison)*
Expr* Parser::parseAnd() {
    Expr* left = parseComparison();
    while (peek(TokenType::AND)) {
        consume(TokenType::AND);
        Expr* right = parseComparison();
        left = new LogicExpr("AND", left, right);
    }
    return left;
}

// comparison -> primary op primary
Expr* Parser::parseComparison() {
    // Handle parenthesised expressions
    if (peek(TokenType::LPAREN)) {
        consume(TokenType::LPAREN);
        Expr* e = parseExpr();
        consume(TokenType::RPAREN);
        return e;
    }

    Expr* left = parsePrimary();

    std::string op;
    if      (peek(TokenType::EQ))  { consume(TokenType::EQ);  op = "=";  }
    else if (peek(TokenType::NEQ)) { consume(TokenType::NEQ); op = "!="; }
    else if (peek(TokenType::GT))  { consume(TokenType::GT);  op = ">";  }
    else if (peek(TokenType::LT))  { consume(TokenType::LT);  op = "<";  }
    else if (peek(TokenType::GTE)) { consume(TokenType::GTE); op = ">="; }
    else if (peek(TokenType::LTE)) { consume(TokenType::LTE); op = "<="; }
    else {
        // No operator — treat the whole thing as a bare expression (shouldn't happen in practice)
        return left;
    }

    Expr* right = parsePrimary();
    return new CompareExpr(op, left, right);
}

// primary -> NUMBER | IDENTIFIER [. IDENTIFIER]
Expr* Parser::parsePrimary() {
    if (peek(TokenType::NUMBER)) {
        int v = std::stoi(cur().text);
        pos++;
        return new NumberExpr(v);
    }
    if (peek(TokenType::IDENTIFIER)) {
        std::string t = cur().text; pos++;
        if (peek(TokenType::DOT)) {
            consume(TokenType::DOT);
            std::string c = consume(TokenType::IDENTIFIER).text;
            return new ColumnExpr(t, c);
        }
        return new ColumnExpr("", t);
    }
    throw std::runtime_error("Expected number or identifier, got: " + cur().text);
}
