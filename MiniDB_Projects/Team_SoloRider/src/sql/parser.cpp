// sql/parser.cpp — Recursive descent SQL parser implementation.
//
// Design notes:
// • Each parse_xxx() method consumes tokens for one grammar production and
//   returns the corresponding AST node.  Errors are reported by throwing
//   std::runtime_error with a descriptive message including the token position.
// • Expression parsing uses precedence climbing:
//   expression → or_expr
//   or_expr    → and_expr (OR and_expr)*
//   and_expr   → comparison (AND comparison)*
//   comparison → primary ((=|!=|<|>|<=|>=) primary)?
//   primary    → literal | column_ref | ( expression )

#include "sql/parser.h"

#include <stdexcept>
#include <sstream>

namespace minidb {

// ─── Constructor ─────────────────────────────────────────────
Parser::Parser(const std::vector<Token>& tokens) : tokens_(tokens), pos_(0) {}

// ─── Token-level helpers ─────────────────────────────────────
const Token& Parser::peek() const {
    // If past the end, return the last token (which should be END_OF_INPUT).
    if (pos_ >= tokens_.size()) {
        return tokens_.back();
    }
    return tokens_[pos_];
}

const Token& Parser::advance() {
    const Token& tok = peek();
    if (pos_ < tokens_.size()) pos_++;
    return tok;
}

const Token& Parser::expect(TokenType t) {
    const Token& tok = peek();
    if (tok.type != t) {
        std::ostringstream oss;
        oss << "Parse error at position " << tok.position
            << ": expected token type " << static_cast<int>(t)
            << " but got '" << tok.value << "'";
        throw std::runtime_error(oss.str());
    }
    return advance();
}

bool Parser::match(TokenType t) {
    if (peek().type == t) {
        advance();
        return true;
    }
    return false;
}

bool Parser::is_at_end() const {
    return pos_ >= tokens_.size() || peek().type == TokenType::END_OF_INPUT;
}

// ─── Entry point ─────────────────────────────────────────────
std::unique_ptr<ASTNode> Parser::parse() {
    const Token& tok = peek();
    switch (tok.type) {
        case TokenType::SELECT: return parse_select();
        case TokenType::INSERT: return parse_insert();
        case TokenType::DELETE: return parse_delete();
        case TokenType::CREATE: return parse_create_table();
        default: {
            std::ostringstream oss;
            oss << "Parse error at position " << tok.position
                << ": unexpected token '" << tok.value
                << "' — expected a statement (SELECT, INSERT, DELETE, CREATE)";
            throw std::runtime_error(oss.str());
        }
    }
}

// ─── SELECT ──────────────────────────────────────────────────
// SELECT columns FROM table [alias]
//   [JOIN table [alias] ON condition]*
//   [WHERE condition]
std::unique_ptr<SelectStmt> Parser::parse_select() {
    expect(TokenType::SELECT);

    auto stmt = std::make_unique<SelectStmt>();

    // Parse column list.
    stmt->columns = parse_column_list();

    // FROM table [alias]
    expect(TokenType::FROM);
    stmt->from_table = expect(TokenType::IDENTIFIER).value;

    // Optional alias (bare IDENTIFIER, or AS IDENTIFIER).
    if (!is_at_end() && peek().type == TokenType::AS) {
        advance();
        stmt->from_alias = expect(TokenType::IDENTIFIER).value;
    } else if (!is_at_end() && peek().type == TokenType::IDENTIFIER) {
        stmt->from_alias = advance().value;
    }

    // Optional JOIN clauses.
    while (!is_at_end()) {
        bool is_join = false;
        if (peek().type == TokenType::JOIN) {
            is_join = true;
            advance();
        } else if (peek().type == TokenType::INNER) {
            advance();
            expect(TokenType::JOIN);
            is_join = true;
        } else if (peek().type == TokenType::LEFT) {
            advance();
            expect(TokenType::JOIN);
            is_join = true;
        }

        if (!is_join) break;

        JoinClause join;
        join.table_name = expect(TokenType::IDENTIFIER).value;

        // Optional alias.
        if (!is_at_end() && peek().type == TokenType::AS) {
            advance();
            join.alias = expect(TokenType::IDENTIFIER).value;
        } else if (!is_at_end() && peek().type == TokenType::IDENTIFIER
                   && peek().type != TokenType::ON) {
            // Peek ahead: only treat as alias if it's not the ON keyword.
            if (peek().type == TokenType::IDENTIFIER) {
                join.alias = advance().value;
            }
        }

        expect(TokenType::ON);
        join.condition = parse_expression();

        stmt->joins.push_back(std::move(join));
    }

    // Optional WHERE clause.
    if (!is_at_end() && match(TokenType::WHERE)) {
        stmt->where_clause = parse_expression();
    }

    // Consume optional trailing semicolon.
    match(TokenType::SEMICOLON);

    return stmt;
}

// ─── INSERT ──────────────────────────────────────────────────
// INSERT INTO table [(col, ...)] VALUES (val, ...) [, (val, ...)]
std::unique_ptr<InsertStmt> Parser::parse_insert() {
    expect(TokenType::INSERT);
    expect(TokenType::INTO);

    auto stmt = std::make_unique<InsertStmt>();
    stmt->table_name = expect(TokenType::IDENTIFIER).value;

    // Optional column-name list.
    if (!is_at_end() && peek().type == TokenType::LPAREN) {
        // Could be column names or could be VALUES — we need to distinguish.
        // Column names come before VALUES keyword.  If the next-next token
        // after LPAREN is an IDENTIFIER (not a number/string), it's column names.
        // Simpler: peek after LPAREN — if VALUES hasn't appeared yet, it's columns.
        advance();  // consume (
        stmt->column_names.push_back(expect(TokenType::IDENTIFIER).value);
        while (match(TokenType::COMMA)) {
            stmt->column_names.push_back(expect(TokenType::IDENTIFIER).value);
        }
        expect(TokenType::RPAREN);
    }

    expect(TokenType::VALUES);

    // Parse one or more value groups: (val, val, ...) [, (val, val, ...)]
    do {
        expect(TokenType::LPAREN);
        std::vector<Value> row;

        // Parse first value.
        row.push_back(parse_value());
        while (match(TokenType::COMMA)) {
            row.push_back(parse_value());
        }

        expect(TokenType::RPAREN);
        stmt->rows.push_back(std::move(row));
    } while (match(TokenType::COMMA));

    match(TokenType::SEMICOLON);
    return stmt;
}

// ─── DELETE ──────────────────────────────────────────────────
// DELETE FROM table [WHERE condition]
std::unique_ptr<DeleteStmt> Parser::parse_delete() {
    expect(TokenType::DELETE);
    expect(TokenType::FROM);

    auto stmt = std::make_unique<DeleteStmt>();
    stmt->table_name = expect(TokenType::IDENTIFIER).value;

    if (!is_at_end() && match(TokenType::WHERE)) {
        stmt->where_clause = parse_expression();
    }

    match(TokenType::SEMICOLON);
    return stmt;
}

// ─── CREATE TABLE ────────────────────────────────────────────
// CREATE TABLE name (col_def, ..., PRIMARY KEY (col))
std::unique_ptr<CreateTableStmt> Parser::parse_create_table() {
    expect(TokenType::CREATE);
    expect(TokenType::TABLE);

    auto stmt = std::make_unique<CreateTableStmt>();
    stmt->table_name = expect(TokenType::IDENTIFIER).value;

    expect(TokenType::LPAREN);

    // Parse column definitions and/or PRIMARY KEY clause.
    while (true) {
        // Check for PRIMARY KEY (col_name)
        if (peek().type == TokenType::PRIMARY) {
            advance();
            expect(TokenType::KEY);
            expect(TokenType::LPAREN);
            stmt->primary_key = expect(TokenType::IDENTIFIER).value;
            expect(TokenType::RPAREN);
        } else {
            // Column definition: name TYPE [(max_length)]
            std::string col_name = expect(TokenType::IDENTIFIER).value;
            ColumnType col_type;
            uint16_t max_len = 0;

            const Token& type_tok = advance();
            switch (type_tok.type) {
                case TokenType::INT_TYPE:     col_type = ColumnType::INT;     break;
                case TokenType::FLOAT_TYPE:   col_type = ColumnType::FLOAT;   break;
                case TokenType::VARCHAR_TYPE: col_type = ColumnType::VARCHAR; break;
                case TokenType::BOOL_TYPE:    col_type = ColumnType::BOOL;    break;
                default: {
                    std::ostringstream oss;
                    oss << "Parse error at position " << type_tok.position
                        << ": expected column type but got '" << type_tok.value << "'";
                    throw std::runtime_error(oss.str());
                }
            }

            // Optional (max_length) for VARCHAR.
            if (match(TokenType::LPAREN)) {
                const Token& len_tok = expect(TokenType::INTEGER_LIT);
                max_len = static_cast<uint16_t>(std::stoi(len_tok.value));
                expect(TokenType::RPAREN);
            }

            stmt->columns.emplace_back(col_name, col_type, max_len);
        }

        // Expect comma (more definitions) or closing paren.
        if (!match(TokenType::COMMA)) break;
    }

    expect(TokenType::RPAREN);
    match(TokenType::SEMICOLON);
    return stmt;
}

// ─── Expression Parsing ─────────────────────────────────────

// Lowest precedence: OR
std::unique_ptr<ASTNode> Parser::parse_expression() {
    auto left = parse_and_expr();

    while (!is_at_end() && peek().type == TokenType::OR) {
        advance();
        auto right = parse_and_expr();
        left = std::make_unique<BinaryExpr>(std::move(left), "OR", std::move(right));
    }

    return left;
}

// AND (higher precedence than OR)
std::unique_ptr<ASTNode> Parser::parse_and_expr() {
    auto left = parse_comparison();

    while (!is_at_end() && peek().type == TokenType::AND) {
        advance();
        auto right = parse_comparison();
        left = std::make_unique<BinaryExpr>(std::move(left), "AND", std::move(right));
    }

    return left;
}

// Comparison operators: =, !=, <, >, <=, >=
std::unique_ptr<ASTNode> Parser::parse_comparison() {
    auto left = parse_primary();

    if (!is_at_end()) {
        std::string op;
        switch (peek().type) {
            case TokenType::EQ:  op = "=";  break;
            case TokenType::NEQ: op = "!="; break;
            case TokenType::LT:  op = "<";  break;
            case TokenType::GT:  op = ">";  break;
            case TokenType::LTE: op = "<="; break;
            case TokenType::GTE: op = ">="; break;
            default: return left;  // No comparison operator — just return LHS.
        }
        advance();
        auto right = parse_primary();
        return std::make_unique<BinaryExpr>(std::move(left), op, std::move(right));
    }

    return left;
}

// Primary expressions: literals, column references, parenthesized expressions.
std::unique_ptr<ASTNode> Parser::parse_primary() {
    const Token& tok = peek();

    switch (tok.type) {
        case TokenType::INTEGER_LIT: {
            advance();
            return std::make_unique<Literal>(Value{std::stoi(tok.value)});
        }
        case TokenType::FLOAT_LIT: {
            advance();
            return std::make_unique<Literal>(Value{std::stod(tok.value)});
        }
        case TokenType::STRING_LIT: {
            advance();
            return std::make_unique<Literal>(Value{tok.value});
        }
        case TokenType::TRUE_KW: {
            advance();
            return std::make_unique<Literal>(Value{true});
        }
        case TokenType::FALSE_KW: {
            advance();
            return std::make_unique<Literal>(Value{false});
        }
        case TokenType::NULL_KW: {
            advance();
            return std::make_unique<Literal>(Value{std::monostate{}});
        }
        case TokenType::IDENTIFIER: {
            std::string name = advance().value;
            // Check for table.column (qualified reference).
            if (!is_at_end() && peek().type == TokenType::DOT) {
                advance();  // consume the dot
                std::string col = expect(TokenType::IDENTIFIER).value;
                return std::make_unique<ColumnRef>(name, col);
            }
            return std::make_unique<ColumnRef>("", name);
        }
        case TokenType::LPAREN: {
            advance();
            auto expr = parse_expression();
            expect(TokenType::RPAREN);
            return expr;
        }
        default: {
            std::ostringstream oss;
            oss << "Parse error at position " << tok.position
                << ": unexpected token '" << tok.value << "' in expression";
            throw std::runtime_error(oss.str());
        }
    }
}

// ─── Column List for SELECT ─────────────────────────────────
std::vector<std::unique_ptr<ASTNode>> Parser::parse_column_list() {
    std::vector<std::unique_ptr<ASTNode>> cols;

    // First column.
    if (peek().type == TokenType::STAR) {
        advance();
        cols.push_back(std::make_unique<StarExpr>());
    } else {
        // Must be a column reference (possibly qualified).
        std::string name = expect(TokenType::IDENTIFIER).value;
        if (!is_at_end() && peek().type == TokenType::DOT) {
            advance();
            std::string col = expect(TokenType::IDENTIFIER).value;
            cols.push_back(std::make_unique<ColumnRef>(name, col));
        } else {
            cols.push_back(std::make_unique<ColumnRef>("", name));
        }
    }

    // Remaining columns, comma-separated.
    while (!is_at_end() && peek().type == TokenType::COMMA) {
        advance();
        if (peek().type == TokenType::STAR) {
            advance();
            cols.push_back(std::make_unique<StarExpr>());
        } else {
            std::string name = expect(TokenType::IDENTIFIER).value;
            if (!is_at_end() && peek().type == TokenType::DOT) {
                advance();
                std::string col = expect(TokenType::IDENTIFIER).value;
                cols.push_back(std::make_unique<ColumnRef>(name, col));
            } else {
                cols.push_back(std::make_unique<ColumnRef>("", name));
            }
        }
    }

    return cols;
}

// ─── Value parser (for INSERT) ───────────────────────────────
// Private helper — not declared in the header because it's only used
// internally by parse_insert.  We declare it here as a private method
// would normally be, but since it isn't in the header we use a
// standalone helper that accesses the parser through a member function.
// Actually, let's just add it as a helper that delegates to parse_primary.
Value Parser::parse_value() {
    const Token& tok = peek();
    switch (tok.type) {
        case TokenType::INTEGER_LIT: {
            advance();
            return Value{std::stoi(tok.value)};
        }
        case TokenType::FLOAT_LIT: {
            advance();
            return Value{std::stod(tok.value)};
        }
        case TokenType::STRING_LIT: {
            advance();
            return Value{tok.value};
        }
        case TokenType::TRUE_KW: {
            advance();
            return Value{true};
        }
        case TokenType::FALSE_KW: {
            advance();
            return Value{false};
        }
        case TokenType::NULL_KW: {
            advance();
            return Value{std::monostate{}};
        }
        default: {
            std::ostringstream oss;
            oss << "Parse error at position " << tok.position
                << ": expected a value but got '" << tok.value << "'";
            throw std::runtime_error(oss.str());
        }
    }
}

// ─── Convenience free function ───────────────────────────────
std::unique_ptr<ASTNode> parse_sql(const std::string& sql) {
    Lexer lexer(sql);
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    return parser.parse();
}

}  // namespace minidb
