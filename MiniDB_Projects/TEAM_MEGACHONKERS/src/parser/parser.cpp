#include "parser/parser.h"
#include "parser/lexer.h"

namespace minidb {

Parser::Parser(std::string sql) {
    Lexer lexer(std::move(sql));
    tokens_ = lexer.Tokenize();
}

// --------------------------------------------------------------------------
// Token cursor helpers
// --------------------------------------------------------------------------
const Token& Parser::Peek() const { return tokens_[pos_]; }

const Token& Parser::Previous() const { return tokens_[pos_ - 1]; }

bool Parser::AtEnd() const { return Peek().type == TokenType::END_OF_FILE; }

bool Parser::Check(TokenType type) const { return Peek().type == type; }

const Token& Parser::Advance() {
    if (!AtEnd()) ++pos_;
    return Previous();
}

bool Parser::Match(TokenType type) {
    if (Check(type)) {
        Advance();
        return true;
    }
    return false;
}

const Token& Parser::Expect(TokenType type, const std::string& what) {
    if (Check(type)) return Advance();
    throw ParseError("Syntax Error: expected " + what + " but found '" +
                     (AtEnd() ? "end of input" : Peek().lexeme) + "'.");
}

// --------------------------------------------------------------------------
// Public entry point
// --------------------------------------------------------------------------
StmtPtr Parser::Parse() {
    try {
        if (AtEnd()) return std::make_unique<InvalidStatement>("Empty statement.");
        return ParseStatement();
    } catch (const ParseError& e) {
        return std::make_unique<InvalidStatement>(e.what());
    } catch (const std::exception& e) {
        return std::make_unique<InvalidStatement>(std::string("Error: ") + e.what());
    }
}

StmtPtr Parser::ParseStatement() {
    switch (Peek().type) {
        case TokenType::KW_CREATE:   return ParseCreate();
        case TokenType::KW_INSERT:   return ParseInsert();
        case TokenType::KW_SELECT:   return ParseSelect();
        case TokenType::KW_DELETE:   return ParseDelete();
        case TokenType::KW_BEGIN:
            Advance();
            return std::make_unique<SimpleStatement>(StatementType::BEGIN_TXN);
        case TokenType::KW_COMMIT:
            Advance();
            return std::make_unique<SimpleStatement>(StatementType::COMMIT_TXN);
        case TokenType::KW_ROLLBACK:
            Advance();
            return std::make_unique<SimpleStatement>(StatementType::ROLLBACK_TXN);
        case TokenType::KW_EXIT:
            Advance();
            return std::make_unique<SimpleStatement>(StatementType::EXIT);
        default:
            throw ParseError("Syntax Error: unrecognized command '" + Peek().lexeme + "'.");
    }
}

// --------------------------------------------------------------------------
// CREATE TABLE / CREATE INDEX
// --------------------------------------------------------------------------
ColumnDefinition Parser::ParseColumnDefinition() {
    const Token& name = Expect(TokenType::IDENTIFIER, "column name");
    ColumnDefinition def;
    def.name = name.lexeme;

    if (Match(TokenType::KW_INT) || Match(TokenType::KW_INTEGER)) {
        def.type = TypeId::INTEGER;
        def.length = 4;
    } else if (Match(TokenType::KW_VARCHAR)) {
        def.type = TypeId::VARCHAR;
        def.length = 255;
    } else {
        throw ParseError("Syntax Error: expected a column type (INT or VARCHAR) for '" +
                         def.name + "'.");
    }
    return def;
}

StmtPtr Parser::ParseCreate() {
    Expect(TokenType::KW_CREATE, "CREATE");

    if (Match(TokenType::KW_TABLE)) {
        auto stmt = std::make_unique<CreateTableStatement>();
        stmt->table_name = Expect(TokenType::IDENTIFIER, "table name").lexeme;
        Expect(TokenType::LPAREN, "'(' before column definitions");
        if (Check(TokenType::RPAREN)) {
            throw ParseError("Syntax Error: a table must have at least one column.");
        }
        do {
            stmt->columns.push_back(ParseColumnDefinition());
        } while (Match(TokenType::COMMA));
        Expect(TokenType::RPAREN, "')' after column definitions");
        Match(TokenType::SEMICOLON);
        return stmt;
    }

    if (Match(TokenType::KW_INDEX)) {
        auto stmt = std::make_unique<CreateIndexStatement>();
        stmt->index_name = Expect(TokenType::IDENTIFIER, "index name").lexeme;
        Expect(TokenType::KW_ON, "ON");
        stmt->table_name = Expect(TokenType::IDENTIFIER, "table name").lexeme;
        Expect(TokenType::LPAREN, "'(' before indexed column");
        stmt->column_name = Expect(TokenType::IDENTIFIER, "column name").lexeme;
        Expect(TokenType::RPAREN, "')' after indexed column");
        Match(TokenType::SEMICOLON);
        return stmt;
    }

    throw ParseError("Syntax Error: expected TABLE or INDEX after CREATE.");
}

// --------------------------------------------------------------------------
// INSERT
// --------------------------------------------------------------------------
std::vector<std::string> Parser::ParseValueTuple() {
    Expect(TokenType::LPAREN, "'(' before VALUES tuple");
    std::vector<std::string> values;
    if (!Check(TokenType::RPAREN)) {
        do {
            const Token& tok = Peek();
            if (tok.type == TokenType::INTEGER_LITERAL ||
                tok.type == TokenType::STRING_LITERAL ||
                tok.type == TokenType::IDENTIFIER) {
                values.push_back(tok.lexeme);
                Advance();
            } else {
                throw ParseError("Syntax Error: expected a literal value but found '" +
                                 tok.lexeme + "'.");
            }
        } while (Match(TokenType::COMMA));
    }
    Expect(TokenType::RPAREN, "')' after VALUES tuple");
    return values;
}

StmtPtr Parser::ParseInsert() {
    Expect(TokenType::KW_INSERT, "INSERT");
    Expect(TokenType::KW_INTO, "INTO");

    auto stmt = std::make_unique<InsertStatement>();
    stmt->table_name = Expect(TokenType::IDENTIFIER, "table name").lexeme;
    Expect(TokenType::KW_VALUES, "VALUES");

    do {
        stmt->rows.push_back(ParseValueTuple());
    } while (Match(TokenType::COMMA));

    Match(TokenType::SEMICOLON);
    return stmt;
}

// --------------------------------------------------------------------------
// SELECT  (projection + JOIN + WHERE)
// --------------------------------------------------------------------------
SelectColumn Parser::ParseColumnRef() {
    const Token& first = Expect(TokenType::IDENTIFIER, "column name");
    SelectColumn col;
    if (Match(TokenType::DOT)) {
        col.table = first.lexeme;
        col.column = Expect(TokenType::IDENTIFIER, "column name after '.'").lexeme;
    } else {
        col.column = first.lexeme;
    }
    return col;
}

StmtPtr Parser::ParseSelect() {
    Expect(TokenType::KW_SELECT, "SELECT");
    auto stmt = std::make_unique<SelectStatement>();

    // Projection list: '*' or a comma-separated list of column references.
    if (Match(TokenType::STAR)) {
        stmt->select_star = true;
    } else {
        stmt->select_star = false;
        do {
            stmt->select_list.push_back(ParseColumnRef());
        } while (Match(TokenType::COMMA));
    }

    Expect(TokenType::KW_FROM, "FROM");
    stmt->table_name = Expect(TokenType::IDENTIFIER, "table name").lexeme;

    // Optional JOIN ... ON <col> = <col>
    if (Match(TokenType::KW_JOIN)) {
        stmt->join.present = true;
        stmt->join.right_table = Expect(TokenType::IDENTIFIER, "joined table name").lexeme;
        Expect(TokenType::KW_ON, "ON");
        stmt->join.left_key = ParseColumnRef();
        Expect(TokenType::EQUAL, "'=' in JOIN condition (only equi-joins supported)");
        stmt->join.right_key = ParseColumnRef();
    }

    // Optional WHERE predicate.
    if (Match(TokenType::KW_WHERE)) {
        stmt->where = ParseExpression();
    }

    Match(TokenType::SEMICOLON);
    return stmt;
}

// --------------------------------------------------------------------------
// DELETE
// --------------------------------------------------------------------------
StmtPtr Parser::ParseDelete() {
    Expect(TokenType::KW_DELETE, "DELETE");
    Expect(TokenType::KW_FROM, "FROM");

    auto stmt = std::make_unique<DeleteStatement>();
    stmt->table_name = Expect(TokenType::IDENTIFIER, "table name").lexeme;

    if (Match(TokenType::KW_WHERE)) {
        stmt->where = ParseExpression();
    }

    Match(TokenType::SEMICOLON);
    return stmt;
}

// --------------------------------------------------------------------------
// Expression grammar (WHERE / ON predicates)
//   or_expr  := and_expr (OR and_expr)*
//   and_expr := predicate (AND predicate)*
//   predicate:= '(' expr ')' | operand compare_op operand
// --------------------------------------------------------------------------
ExprPtr Parser::ParseExpression() { return ParseOr(); }

ExprPtr Parser::ParseOr() {
    ExprPtr left = ParseAnd();
    while (Match(TokenType::KW_OR)) {
        ExprPtr right = ParseAnd();
        left = std::make_unique<LogicalExpression>(LogicOp::OR, std::move(left), std::move(right));
    }
    return left;
}

ExprPtr Parser::ParseAnd() {
    ExprPtr left = ParsePredicate();
    while (Match(TokenType::KW_AND)) {
        ExprPtr right = ParsePredicate();
        left = std::make_unique<LogicalExpression>(LogicOp::AND, std::move(left), std::move(right));
    }
    return left;
}

ExprPtr Parser::ParsePredicate() {
    if (Match(TokenType::LPAREN)) {
        ExprPtr inner = ParseExpression();
        Expect(TokenType::RPAREN, "')' to close a grouped expression");
        return inner;
    }

    ExprPtr left = ParseOperand();

    CompareOp op;
    if (Match(TokenType::EQUAL))             op = CompareOp::EQ;
    else if (Match(TokenType::NOT_EQUAL))    op = CompareOp::NE;
    else if (Match(TokenType::LESS))         op = CompareOp::LT;
    else if (Match(TokenType::LESS_EQUAL))   op = CompareOp::LE;
    else if (Match(TokenType::GREATER))      op = CompareOp::GT;
    else if (Match(TokenType::GREATER_EQUAL))op = CompareOp::GE;
    else throw ParseError("Syntax Error: expected a comparison operator (=, !=, <, <=, >, >=).");

    ExprPtr right = ParseOperand();
    return std::make_unique<ComparisonExpression>(op, std::move(left), std::move(right));
}

ExprPtr Parser::ParseOperand() {
    const Token& tok = Peek();
    if (tok.type == TokenType::INTEGER_LITERAL || tok.type == TokenType::STRING_LITERAL) {
        Advance();
        return std::make_unique<ConstantExpression>(tok.lexeme);
    }
    if (tok.type == TokenType::IDENTIFIER) {
        SelectColumn col = ParseColumnRef();
        return std::make_unique<ColumnRefExpression>(col.table, col.column);
    }
    throw ParseError("Syntax Error: expected a column or literal but found '" + tok.lexeme + "'.");
}

} // namespace minidb
