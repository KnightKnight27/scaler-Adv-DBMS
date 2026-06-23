#include "parser.h"

#include <stdexcept>

namespace minidb {

bool Parser::AcceptKw(const std::string& kw) {
    if (Peek().kind == TokKind::Keyword && Peek().text == kw) { ++pos_; return true; }
    return false;
}
bool Parser::AcceptPunct(const std::string& p) {
    if ((Peek().kind == TokKind::Punct || Peek().kind == TokKind::Op) && Peek().text == p) { ++pos_; return true; }
    return false;
}
void Parser::ExpectKw(const std::string& kw) {
    if (!AcceptKw(kw)) throw std::runtime_error("expected keyword " + kw + " near '" + Peek().raw + "'");
}
void Parser::ExpectPunct(const std::string& p) {
    if (!AcceptPunct(p)) throw std::runtime_error("expected '" + p + "' near '" + Peek().raw + "'");
}

std::unique_ptr<Statement> Parser::Parse() {
    if (AcceptKw("CREATE")) return ParseCreate();
    if (AcceptKw("INSERT")) return ParseInsert();
    if (AcceptKw("SELECT")) return ParseSelect();
    if (AcceptKw("DELETE")) return ParseDelete();
    if (AcceptKw("BEGIN"))  { auto s = std::make_unique<TxnStmt>(); s->op = TxnStmt::Op::Begin;  return s; }
    if (AcceptKw("COMMIT")) { auto s = std::make_unique<TxnStmt>(); s->op = TxnStmt::Op::Commit; return s; }
    if (AcceptKw("ABORT") || AcceptKw("ROLLBACK")) { auto s = std::make_unique<TxnStmt>(); s->op = TxnStmt::Op::Abort; return s; }
    throw std::runtime_error("unknown statement near '" + Peek().raw + "'");
}

TypeId Parser::ParseType() {
    const Token& t = Next();
    TypeId ty = TypeIdFromString(t.text);
    if (ty == TypeId::INVALID) throw std::runtime_error("unknown type '" + t.raw + "'");
    // Optional width, e.g. VARCHAR(32): accepted and ignored (variable-length storage).
    if (AcceptPunct("(")) { Next(); ExpectPunct(")"); }
    return ty;
}

std::unique_ptr<Statement> Parser::ParseCreate() {
    bool unique = AcceptKw("UNIQUE");
    if (!unique && AcceptKw("TABLE")) {
        auto s = std::make_unique<CreateTableStmt>();
        s->table = Next().raw;  // table name
        ExpectPunct("(");
        do {
            Column col;
            col.name = Next().raw;
            col.type = ParseType();
            if (AcceptKw("PRIMARY")) { ExpectKw("KEY"); col.is_primary_key = true; }
            s->columns.push_back(col);
        } while (AcceptPunct(","));
        ExpectPunct(")");
        return s;
    }
    // CREATE [UNIQUE] INDEX [name] ON table (col)
    ExpectKw("INDEX");
    auto s = std::make_unique<CreateIndexStmt>();
    s->unique = unique;
    if (Peek().kind == TokKind::Ident) Next();  // optional index name
    ExpectKw("ON");
    s->table = Next().raw;
    ExpectPunct("(");
    s->column = Next().raw;
    ExpectPunct(")");
    return s;
}

std::unique_ptr<Statement> Parser::ParseInsert() {
    ExpectKw("INTO");
    auto s = std::make_unique<InsertStmt>();
    s->table = Next().raw;
    if (AcceptPunct("(")) {
        do { s->columns.push_back(Next().raw); } while (AcceptPunct(","));
        ExpectPunct(")");
    }
    ExpectKw("VALUES");
    do {
        ExpectPunct("(");
        std::vector<Value> row;
        do { row.push_back(ParseLiteral()); } while (AcceptPunct(","));
        ExpectPunct(")");
        s->rows.push_back(std::move(row));
    } while (AcceptPunct(","));
    return s;
}

std::unique_ptr<Statement> Parser::ParseSelect() {
    auto s = std::make_unique<SelectStmt>();
    if (AcceptPunct("*")) {
        s->select_list.push_back("*");
    } else {
        do { s->select_list.push_back(ParseColumnRef()); } while (AcceptPunct(","));
    }
    ExpectKw("FROM");
    s->from_table = Next().raw;
    AcceptKw("INNER");
    if (AcceptKw("JOIN")) {
        s->join.present = true;
        s->join.table = Next().raw;
        ExpectKw("ON");
        s->join.left_col = ParseColumnRef();
        ExpectPunct("=");
        s->join.right_col = ParseColumnRef();
    }
    if (AcceptKw("WHERE")) s->where = ParseExpr();
    if (AcceptKw("FOR")) { ExpectKw("UPDATE"); s->for_update = true; }
    return s;
}

std::unique_ptr<Statement> Parser::ParseDelete() {
    ExpectKw("FROM");
    auto s = std::make_unique<DeleteStmt>();
    s->table = Next().raw;
    if (AcceptKw("WHERE")) s->where = ParseExpr();
    return s;
}

// ---------- expressions ----------
std::unique_ptr<Expr> Parser::ParseExpr() { return ParseOr(); }

std::unique_ptr<Expr> Parser::ParseOr() {
    auto left = ParseAnd();
    while (AcceptKw("OR")) left = Expr::Bin("OR", std::move(left), ParseAnd());
    return left;
}
std::unique_ptr<Expr> Parser::ParseAnd() {
    auto left = ParseCmp();
    while (AcceptKw("AND")) left = Expr::Bin("AND", std::move(left), ParseCmp());
    return left;
}
std::unique_ptr<Expr> Parser::ParseCmp() {
    auto left = ParsePrimary();
    const std::string& op = Peek().text;
    if (Peek().kind == TokKind::Op &&
        (op == "=" || op == "<" || op == ">" || op == "<=" || op == ">=" || op == "!=")) {
        std::string o = Next().text;
        return Expr::Bin(o, std::move(left), ParsePrimary());
    }
    return left;
}
std::unique_ptr<Expr> Parser::ParsePrimary() {
    if (AcceptPunct("(")) {
        auto e = ParseExpr();
        ExpectPunct(")");
        return e;
    }
    if (Peek().kind == TokKind::Ident) return Expr::Col(ParseColumnRef());
    return Expr::Const(ParseLiteral());
}

std::string Parser::ParseColumnRef() {
    std::string name = Next().raw;
    if (AcceptPunct(".")) name += "." + Next().raw;
    return name;
}

Value Parser::ParseLiteral() {
    const Token& t = Next();
    if (t.kind == TokKind::Number) {
        long long v = std::stoll(t.text);
        if (v >= INT32_MIN && v <= INT32_MAX) return Value::MakeInt(static_cast<int32_t>(v));
        return Value::MakeBigInt(v);
    }
    if (t.kind == TokKind::String) return Value::MakeVarchar(t.raw);
    if (t.kind == TokKind::Keyword && t.text == "TRUE")  return Value::MakeBool(true);
    if (t.kind == TokKind::Keyword && t.text == "FALSE") return Value::MakeBool(false);
    throw std::runtime_error("expected a literal near '" + t.raw + "'");
}

}  // namespace minidb
