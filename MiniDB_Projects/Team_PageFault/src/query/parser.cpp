#include "parser.hpp"
#include <stdexcept>

namespace minidb {

const Token& Parser::expect(Tok t, const char* what) {
    if (!check(t)) throw std::runtime_error(std::string("parse error: expected ") + what +
                                            " but got '" + peek().text + "'");
    return get();
}

Statement Parser::parse() {
    Statement s = parseStatement();
    accept(Tok::SEMI);
    // Reject trailing junk after a complete statement (catches typos and
    // accidental multi-statement input).
    if (!check(Tok::END))
        throw std::runtime_error("parse error: unexpected trailing token '" + peek().text + "'");
    return s;
}

Statement Parser::parseStatement() {
    switch (peek().type) {
        case Tok::CREATE: return parseCreate();
        case Tok::INSERT: return parseInsert();
        case Tok::SELECT: return parseSelect();
        case Tok::DELETE: return parseDelete();
        case Tok::BEGIN:    get(); { Statement s; s.kind = Statement::Kind::BEGIN; return s; }
        case Tok::COMMIT:   get(); { Statement s; s.kind = Statement::Kind::COMMIT; return s; }
        case Tok::ROLLBACK: get(); { Statement s; s.kind = Statement::Kind::ROLLBACK; return s; }
        default:
            throw std::runtime_error("parse error: unexpected token '" + peek().text + "'");
    }
}

Statement Parser::parseCreate() {
    expect(Tok::CREATE, "CREATE");
    expect(Tok::TABLE, "TABLE");
    Statement s;
    s.kind = Statement::Kind::CREATE;
    s.create.table = expect(Tok::IDENT, "table name").text;
    expect(Tok::LPAREN, "(");
    do {
        std::string name = expect(Tok::IDENT, "column name").text;
        Type ty;
        if (accept(Tok::KW_INT)) ty = Type::INT;
        else if (accept(Tok::KW_TEXT)) ty = Type::TEXT;
        else throw std::runtime_error("parse error: expected INT or TEXT for column " + name);
        s.create.cols.push_back(Column{name, ty});
    } while (accept(Tok::COMMA));
    expect(Tok::RPAREN, ")");
    accept(Tok::SEMI);
    return s;
}

Statement Parser::parseInsert() {
    expect(Tok::INSERT, "INSERT");
    expect(Tok::INTO, "INTO");
    Statement s;
    s.kind = Statement::Kind::INSERT;
    s.insert.table = expect(Tok::IDENT, "table name").text;
    expect(Tok::VALUES, "VALUES");
    expect(Tok::LPAREN, "(");
    do {
        s.insert.values.push_back(parseLiteral());
    } while (accept(Tok::COMMA));
    expect(Tok::RPAREN, ")");
    accept(Tok::SEMI);
    return s;
}

Statement Parser::parseSelect() {
    expect(Tok::SELECT, "SELECT");
    Statement s;
    s.kind = Statement::Kind::SELECT;

    if (accept(Tok::STAR)) {
        s.select.star = true;
    } else if (check(Tok::COUNT)) {
        get();
        expect(Tok::LPAREN, "(");
        expect(Tok::STAR, "*");
        expect(Tok::RPAREN, ")");
        s.select.count_star = true;
    } else {
        do {
            s.select.cols.push_back(parseColRef());
        } while (accept(Tok::COMMA));
    }

    expect(Tok::FROM, "FROM");
    s.select.table = expect(Tok::IDENT, "table name").text;

    if (accept(Tok::JOIN)) {
        s.select.join.present = true;
        s.select.join.table = expect(Tok::IDENT, "joined table name").text;
        expect(Tok::ON, "ON");
        s.select.join.left_col = parseColRef();
        expect(Tok::EQ, "= (only equi-joins are supported)");
        s.select.join.right_col = parseColRef();
    }

    if (accept(Tok::WHERE)) s.select.where = parseExpr();
    accept(Tok::SEMI);
    return s;
}

Statement Parser::parseDelete() {
    expect(Tok::DELETE, "DELETE");
    expect(Tok::FROM, "FROM");
    Statement s;
    s.kind = Statement::Kind::DELETE;
    s.del.table = expect(Tok::IDENT, "table name").text;
    if (accept(Tok::WHERE)) s.del.where = parseExpr();
    accept(Tok::SEMI);
    return s;
}

std::string Parser::parseColRef() {
    std::string name = expect(Tok::IDENT, "column name").text;
    if (accept(Tok::DOT)) name += "." + expect(Tok::IDENT, "column name after '.'").text;
    return name;
}

Value Parser::parseLiteral() {
    if (check(Tok::INT_LIT)) return Value::makeInt(get().ival);
    if (check(Tok::STR_LIT)) return Value::makeText(get().text);
    throw std::runtime_error("parse error: expected a literal value");
}

std::unique_ptr<Expr> Parser::parseExpr() {
    auto left = parseAnd();
    while (accept(Tok::OR)) {
        auto node = std::make_unique<Expr>();
        node->kind = Expr::Kind::OR;
        node->left = std::move(left);
        node->right = parseAnd();
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseAnd() {
    auto left = parseCmp();
    while (accept(Tok::AND)) {
        auto node = std::make_unique<Expr>();
        node->kind = Expr::Kind::AND;
        node->left = std::move(left);
        node->right = parseCmp();
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parseCmp() {
    // Bound recursion so pathological input (deeply nested parentheses) raises a
    // clean error instead of overflowing the stack.
    struct DepthGuard {
        int& d;
        explicit DepthGuard(int& dd) : d(dd) {
            if (++d > 500) throw std::runtime_error("parse error: expression nested too deeply");
        }
        ~DepthGuard() { d--; }
    } guard(depth_);

    if (accept(Tok::LPAREN)) {
        auto e = parseExpr();
        expect(Tok::RPAREN, ")");
        return e;
    }
    auto e = std::make_unique<Expr>();
    e->kind = Expr::Kind::CMP;
    e->col = parseColRef();
    switch (peek().type) {
        case Tok::EQ: e->op = Expr::Op::EQ; break;
        case Tok::NE: e->op = Expr::Op::NE; break;
        case Tok::LT: e->op = Expr::Op::LT; break;
        case Tok::LE: e->op = Expr::Op::LE; break;
        case Tok::GT: e->op = Expr::Op::GT; break;
        case Tok::GE: e->op = Expr::Op::GE; break;
        default: throw std::runtime_error("parse error: expected a comparison operator");
    }
    get();
    e->val = parseLiteral();
    return e;
}

} // namespace minidb
