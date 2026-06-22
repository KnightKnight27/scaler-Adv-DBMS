#include "parser.h"
#include <cctype>
#include <algorithm>
#include <sstream>
#include <stdexcept>

// ─── Tokenizer ────────────────────────────────────────────────────────────────

static std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

static TokenType keyword_type(const std::string& upper) {
    if (upper == "SELECT")   return TokenType::SELECT;
    if (upper == "FROM")     return TokenType::FROM;
    if (upper == "WHERE")    return TokenType::WHERE;
    if (upper == "JOIN")     return TokenType::JOIN;
    if (upper == "ON")       return TokenType::ON;
    if (upper == "INSERT")   return TokenType::INSERT;
    if (upper == "INTO")     return TokenType::INTO;
    if (upper == "VALUES")   return TokenType::VALUES;
    if (upper == "DELETE")   return TokenType::DELETE;
    if (upper == "CREATE")   return TokenType::CREATE;
    if (upper == "TABLE")    return TokenType::TABLE;
    if (upper == "DROP")     return TokenType::DROP;
    if (upper == "BEGIN")    return TokenType::BEGIN;
    if (upper == "COMMIT")   return TokenType::COMMIT;
    if (upper == "ROLLBACK") return TokenType::ROLLBACK;
    if (upper == "AND")      return TokenType::AND;
    if (upper == "OR")       return TokenType::OR;
    if (upper == "NOT")      return TokenType::NOT;
    if (upper == "INT")      return TokenType::INT_KW;
    if (upper == "VARCHAR")  return TokenType::VARCHAR_KW;
    if (upper == "PRIMARY")  return TokenType::PRIMARY;
    if (upper == "KEY")      return TokenType::KEY;
    return TokenType::IDENT;
}

std::vector<Token> Parser::tokenize(const std::string& sql) {
    std::vector<Token> tokens;
    size_t i = 0, n = sql.size();

    while (i < n) {
        // Skip whitespace
        if (std::isspace(sql[i])) { ++i; continue; }

        // Single-line comment
        if (i+1 < n && sql[i] == '-' && sql[i+1] == '-') {
            while (i < n && sql[i] != '\n') ++i;
            continue;
        }

        // String literal  'hello'
        if (sql[i] == '\'') {
            ++i;
            std::string s;
            while (i < n && sql[i] != '\'') s += sql[i++];
            if (i < n) ++i; // consume closing '
            tokens.push_back({TokenType::STR_LIT, s});
            continue;
        }

        // Integer literal
        if (std::isdigit(sql[i]) || (sql[i] == '-' && i+1 < n && std::isdigit(sql[i+1]))) {
            std::string num;
            if (sql[i] == '-') num += sql[i++];
            while (i < n && std::isdigit(sql[i])) num += sql[i++];
            Token t{TokenType::INT_LIT, num};
            t.int_val = std::stoll(num);
            tokens.push_back(t);
            continue;
        }

        // Identifier or keyword
        if (std::isalpha(sql[i]) || sql[i] == '_') {
            std::string word;
            while (i < n && (std::isalnum(sql[i]) || sql[i] == '_')) word += sql[i++];
            std::string up = to_upper(word);
            tokens.push_back({keyword_type(up), word});
            continue;
        }

        // table.column  — split on dot into two IDENTs separated by a DOT
        // We represent table.column as two tokens: IDENT '.' IDENT.
        // The parser handles the dot implicitly via expect(IDENT) twice.

        // Two-char operators
        if (i+1 < n) {
            std::string two{sql[i], sql[i+1]};
            if (two == "!=") { tokens.push_back({TokenType::NEQ, two}); i+=2; continue; }
            if (two == "<=") { tokens.push_back({TokenType::LTE, two}); i+=2; continue; }
            if (two == ">=") { tokens.push_back({TokenType::GTE, two}); i+=2; continue; }
            if (two == "<>") { tokens.push_back({TokenType::NEQ, two}); i+=2; continue; }
        }

        // Single-char tokens
        switch (sql[i]) {
            case '=': tokens.push_back({TokenType::EQ,     "="}); break;
            case '<': tokens.push_back({TokenType::LT,     "<"}); break;
            case '>': tokens.push_back({TokenType::GT,     ">"}); break;
            case ',': tokens.push_back({TokenType::COMMA,  ","}); break;
            case '(': tokens.push_back({TokenType::LPAREN, "("}); break;
            case ')': tokens.push_back({TokenType::RPAREN, ")"}); break;
            case '*': tokens.push_back({TokenType::STAR,   "*"}); break;
            case ';': tokens.push_back({TokenType::SEMI,   ";"}); break;
            case '.': tokens.push_back({TokenType::IDENT,  "."}); break; // dot separator
            default:
                throw std::runtime_error(std::string("Unexpected character: ") + sql[i]);
        }
        ++i;
    }
    tokens.push_back({TokenType::END_OF_INPUT, ""});
    return tokens;
}

// ─── Token stream helpers ─────────────────────────────────────────────────────

Parser::Parser(const std::string& sql) : tokens_(tokenize(sql)) {}

Token& Parser::peek() { return tokens_[pos_]; }

Token& Parser::advance() { return tokens_[pos_++]; }

Token& Parser::expect(TokenType t, const std::string& msg) {
    if (peek().type != t)
        throw std::runtime_error("Parse error: " + msg +
                                 " (got '" + peek().lexeme + "')");
    return advance();
}

bool Parser::match(TokenType t) {
    if (peek().type == t) { advance(); return true; }
    return false;
}

// ─── Top-level ────────────────────────────────────────────────────────────────

Statement Parser::parse() {
    Statement stmt = parse_statement();
    match(TokenType::SEMI); // optional trailing semicolon
    return stmt;
}

Statement Parser::parse_statement() {
    switch (peek().type) {
        case TokenType::SELECT:   return parse_select();
        case TokenType::INSERT:   return parse_insert();
        case TokenType::DELETE:   return parse_delete();
        case TokenType::CREATE:   return parse_create();
        case TokenType::DROP:     return parse_drop();
        case TokenType::BEGIN:    advance(); return BeginStmt{};
        case TokenType::COMMIT:   advance(); return CommitStmt{};
        case TokenType::ROLLBACK: advance(); return RollbackStmt{};
        default:
            throw std::runtime_error("Unknown statement starting with: " + peek().lexeme);
    }
}

// ─── SELECT ───────────────────────────────────────────────────────────────────
// SELECT * FROM t [JOIN t2 ON cond] [WHERE cond]
// SELECT c1, c2 FROM t [JOIN t2 ON cond] [WHERE cond]
SelectStmt Parser::parse_select() {
    expect(TokenType::SELECT, "SELECT");
    SelectStmt s;

    if (peek().type == TokenType::STAR) {
        advance(); s.star = true;
    } else {
        s.columns.push_back(expect(TokenType::IDENT, "column name").lexeme);
        while (match(TokenType::COMMA))
            s.columns.push_back(expect(TokenType::IDENT, "column name").lexeme);
    }

    expect(TokenType::FROM, "FROM");
    s.table = expect(TokenType::IDENT, "table name").lexeme;

    if (peek().type == TokenType::JOIN) {
        advance();
        s.has_join    = true;
        s.join_table  = expect(TokenType::IDENT, "join table").lexeme;
        expect(TokenType::ON, "ON");
        s.join_cond   = parse_condition();
    }

    if (peek().type == TokenType::WHERE) {
        advance();
        s.has_where  = true;
        s.where_cond = parse_condition();
    }
    return s;
}

// ─── INSERT ──────────────────────────────────────────────────────────────────
// INSERT INTO table VALUES (v1, v2, ...)
InsertStmt Parser::parse_insert() {
    expect(TokenType::INSERT, "INSERT");
    expect(TokenType::INTO,   "INTO");
    InsertStmt s;
    s.table = expect(TokenType::IDENT, "table name").lexeme;
    expect(TokenType::VALUES, "VALUES");
    expect(TokenType::LPAREN, "(");
    s.values.push_back(parse_literal());
    while (match(TokenType::COMMA))
        s.values.push_back(parse_literal());
    expect(TokenType::RPAREN, ")");
    return s;
}

// ─── DELETE ───────────────────────────────────────────────────────────────────
// DELETE FROM table [WHERE cond]
DeleteStmt Parser::parse_delete() {
    expect(TokenType::DELETE, "DELETE");
    expect(TokenType::FROM,   "FROM");
    DeleteStmt s;
    s.table = expect(TokenType::IDENT, "table name").lexeme;
    if (peek().type == TokenType::WHERE) {
        advance();
        s.has_where  = true;
        s.where_cond = parse_condition();
    }
    return s;
}

// ─── CREATE TABLE ────────────────────────────────────────────────────────────
// CREATE TABLE name (col type [PRIMARY KEY], ...)
CreateStmt Parser::parse_create() {
    expect(TokenType::CREATE, "CREATE");
    expect(TokenType::TABLE,  "TABLE");
    CreateStmt s;
    s.table = expect(TokenType::IDENT, "table name").lexeme;
    expect(TokenType::LPAREN, "(");

    do {
        ColDef col;
        col.name = expect(TokenType::IDENT, "column name").lexeme;
        if (peek().type == TokenType::INT_KW) {
            advance(); col.type = Type::INT;
        } else if (peek().type == TokenType::VARCHAR_KW) {
            advance(); col.type = Type::VARCHAR;
            // Optionally consume (size) — we ignore the size
            if (peek().type == TokenType::LPAREN) {
                advance();
                expect(TokenType::INT_LIT, "size");
                expect(TokenType::RPAREN,  ")");
            }
        } else {
            throw std::runtime_error("Expected column type, got: " + peek().lexeme);
        }
        if (peek().type == TokenType::PRIMARY) {
            advance();
            expect(TokenType::KEY, "KEY");
            col.primary_key = true;
        }
        s.cols.push_back(col);
    } while (match(TokenType::COMMA));

    expect(TokenType::RPAREN, ")");
    return s;
}

DropStmt Parser::parse_drop() {
    expect(TokenType::DROP,  "DROP");
    expect(TokenType::TABLE, "TABLE");
    return {expect(TokenType::IDENT, "table name").lexeme};
}

// ─── Condition parser ────────────────────────────────────────────────────────
// col OP value  |  table.col OP table.col  |  col OP col
Condition Parser::parse_condition() {
    Condition c;
    // Left side: possibly table.col
    c.left_col = expect(TokenType::IDENT, "column name").lexeme;
    if (peek().type == TokenType::IDENT && peek().lexeme == ".") {
        advance(); // consume dot
        c.left_table = c.left_col;
        c.left_col   = expect(TokenType::IDENT, "column name").lexeme;
    }

    // Operator
    c.op = peek().type;
    if (c.op != TokenType::EQ  && c.op != TokenType::NEQ &&
        c.op != TokenType::LT  && c.op != TokenType::LTE &&
        c.op != TokenType::GT  && c.op != TokenType::GTE)
        throw std::runtime_error("Expected comparison operator");
    advance();

    // Right side: literal or column reference
    if (peek().type == TokenType::INT_LIT) {
        c.rhs_is_col = false;
        c.rhs_val = Value::make_int(peek().int_val);
        advance();
    } else if (peek().type == TokenType::STR_LIT) {
        c.rhs_is_col = false;
        c.rhs_val = Value::make_varchar(peek().lexeme);
        advance();
    } else if (peek().type == TokenType::IDENT) {
        c.rhs_is_col = true;
        c.rhs_col    = advance().lexeme;
        if (peek().type == TokenType::IDENT && peek().lexeme == ".") {
            advance();
            c.rhs_table = c.rhs_col;
            c.rhs_col   = expect(TokenType::IDENT, "column name").lexeme;
        }
    } else {
        throw std::runtime_error("Expected value or column in condition");
    }
    return c;
}

Value Parser::parse_literal() {
    if (peek().type == TokenType::INT_LIT) {
        auto v = Value::make_int(peek().int_val);
        advance(); return v;
    }
    if (peek().type == TokenType::STR_LIT) {
        auto v = Value::make_varchar(peek().lexeme);
        advance(); return v;
    }
    throw std::runtime_error("Expected literal value, got: " + peek().lexeme);
}
