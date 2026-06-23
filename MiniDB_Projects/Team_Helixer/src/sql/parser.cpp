#include "sql/parser.h"
#include <cctype>
#include <stdexcept>
#include <vector>

namespace minidb {
namespace {

// ---- Lexer ---------------------------------------------------------------
enum class Tok { IDENT, INT_LIT, STR_LIT, PUNCT, END };

struct Token {
    Tok         type;
    std::string text;  // identifiers/punctuation text, or raw string contents
    int32_t     ival{0};
};

std::string upper(std::string s) {
    for (char &c : s) c = static_cast<char>(std::toupper((unsigned char)c));
    return s;
}

std::vector<Token> Lex(const std::string &s) {
    std::vector<Token> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        char c = s[i];
        if (std::isspace((unsigned char)c)) { ++i; continue; }
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t j = i;
            while (j < n && (std::isalnum((unsigned char)s[j]) || s[j] == '_')) ++j;
            out.push_back({Tok::IDENT, s.substr(i, j - i), 0});
            i = j;
        } else if (std::isdigit((unsigned char)c) ||
                   (c == '-' && i + 1 < n && std::isdigit((unsigned char)s[i + 1]))) {
            size_t j = i + (c == '-' ? 1 : 0);
            while (j < n && std::isdigit((unsigned char)s[j])) ++j;
            Token t{Tok::INT_LIT, s.substr(i, j - i), 0};
            t.ival = std::stoi(t.text);
            out.push_back(t);
            i = j;
        } else if (c == '\'') {                      // 'string literal'
            size_t j = i + 1;
            std::string val;
            while (j < n && s[j] != '\'') val.push_back(s[j++]);
            if (j >= n) throw std::runtime_error("unterminated string literal");
            out.push_back({Tok::STR_LIT, val, 0});
            i = j + 1;
        } else {
            // Multi-char operators: <=, >=, !=, <>
            std::string op(1, c);
            if ((c == '<' || c == '>' || c == '!') && i + 1 < n &&
                (s[i + 1] == '=' || s[i + 1] == '>')) {
                op.push_back(s[i + 1]);
                ++i;
            }
            out.push_back({Tok::PUNCT, op, 0});
            ++i;
        }
    }
    out.push_back({Tok::END, "", 0});
    return out;
}

// ---- Recursive-descent parser -------------------------------------------
class Parser {
public:
    explicit Parser(std::vector<Token> toks) : toks_(std::move(toks)) {}

    std::unique_ptr<Statement> parse() {
        std::string kw = upper(peek().text);
        if (kw == "CREATE") return parse_create();
        if (kw == "INSERT") return parse_insert();
        if (kw == "SELECT") return parse_select();
        if (kw == "DELETE") return parse_delete();
        if (kw == "BEGIN" || kw == "START")  { return make_txn(StmtType::BEGIN); }
        if (kw == "COMMIT") { return make_txn(StmtType::COMMIT); }
        if (kw == "ABORT" || kw == "ROLLBACK") { return make_txn(StmtType::ABORT); }
        throw std::runtime_error("unsupported statement: " + peek().text);
    }

private:
    const Token &peek() const { return toks_[pos_]; }
    const Token &advance() { return toks_[pos_++]; }
    bool is_end() const { return peek().type == Tok::END; }

    bool accept_kw(const std::string &kw) {
        if (peek().type == Tok::IDENT && upper(peek().text) == kw) { ++pos_; return true; }
        return false;
    }
    void expect_kw(const std::string &kw) {
        if (!accept_kw(kw)) throw std::runtime_error("expected keyword " + kw + " but got '" + peek().text + "'");
    }
    bool accept_punct(const std::string &p) {
        if (peek().type == Tok::PUNCT && peek().text == p) { ++pos_; return true; }
        return false;
    }
    void expect_punct(const std::string &p) {
        if (!accept_punct(p)) throw std::runtime_error("expected '" + p + "' but got '" + peek().text + "'");
    }
    std::string expect_ident() {
        if (peek().type != Tok::IDENT) throw std::runtime_error("expected identifier but got '" + peek().text + "'");
        return advance().text;
    }

    std::unique_ptr<Statement> make_txn(StmtType t) { advance(); return std::make_unique<TxnStmt>(t); }

    TypeId parse_type() {
        std::string t = upper(expect_ident());
        TypeId ty;
        if (t == "INT" || t == "INTEGER")               ty = TypeId::INTEGER;
        else if (t == "VARCHAR" || t == "TEXT" || t == "STRING") ty = TypeId::VARCHAR;
        else throw std::runtime_error("unknown column type: " + t);
        // Optional length spec, e.g. VARCHAR(32) — parsed and ignored.
        if (accept_punct("(")) { advance(); expect_punct(")"); }
        return ty;
    }

    ColumnRef parse_colref() {
        ColumnRef c;
        std::string first = expect_ident();
        if (accept_punct(".")) { c.table = first; c.column = expect_ident(); }
        else { c.column = first; }
        return c;
    }

    Operand parse_operand() {
        Operand o;
        if (peek().type == Tok::INT_LIT)      { o.is_column = false; o.constant = Value(advance().ival); }
        else if (peek().type == Tok::STR_LIT) { o.is_column = false; o.constant = Value(advance().text); }
        else                                  { o.is_column = true;  o.col = parse_colref(); }
        return o;
    }

    CompOp parse_op() {
        std::string p = advance().text;
        if (p == "=")  return CompOp::EQ;
        if (p == "!=" || p == "<>") return CompOp::NE;
        if (p == "<")  return CompOp::LT;
        if (p == "<=") return CompOp::LE;
        if (p == ">")  return CompOp::GT;
        if (p == ">=") return CompOp::GE;
        throw std::runtime_error("expected comparison operator but got '" + p + "'");
    }

    std::vector<Predicate> parse_where() {
        std::vector<Predicate> preds;
        if (!accept_kw("WHERE")) return preds;
        while (true) {
            Predicate pr;
            pr.left = parse_operand();
            pr.op = parse_op();
            pr.right = parse_operand();
            preds.push_back(pr);
            if (!accept_kw("AND")) break;
        }
        return preds;
    }

    std::unique_ptr<Statement> parse_create() {
        expect_kw("CREATE"); expect_kw("TABLE");
        auto stmt = std::make_unique<CreateTableStmt>();
        stmt->name = expect_ident();
        expect_punct("(");
        while (true) {
            if (peek().type == Tok::IDENT && upper(peek().text) == "PRIMARY") {
                advance(); expect_kw("KEY"); expect_punct("(");
                std::string pkcol = expect_ident();
                expect_punct(")");
                // Columns are declared before PRIMARY KEY, so index_of resolves.
                stmt->schema.pk_index = stmt->schema.index_of(pkcol);
            } else {
                Column col;
                col.name = expect_ident();
                col.type = parse_type();
                stmt->schema.columns.push_back(col);
            }
            if (accept_punct(",")) continue;
            break;
        }
        expect_punct(")");
        accept_punct(";");
        return stmt;
    }

    std::unique_ptr<Statement> parse_insert() {
        expect_kw("INSERT"); expect_kw("INTO");
        auto stmt = std::make_unique<InsertStmt>();
        stmt->table = expect_ident();
        expect_kw("VALUES");
        expect_punct("(");
        while (true) {
            if (peek().type == Tok::INT_LIT)      stmt->values.emplace_back(advance().ival);
            else if (peek().type == Tok::STR_LIT) stmt->values.emplace_back(advance().text);
            else throw std::runtime_error("expected literal in VALUES");
            if (accept_punct(",")) continue;
            break;
        }
        expect_punct(")");
        accept_punct(";");
        return stmt;
    }

    std::unique_ptr<Statement> parse_select() {
        expect_kw("SELECT");
        auto stmt = std::make_unique<SelectStmt>();
        if (accept_punct("*")) {
            stmt->select_star = true;
        } else {
            while (true) {
                stmt->columns.push_back(parse_colref());
                if (accept_punct(",")) continue;
                break;
            }
        }
        expect_kw("FROM");
        stmt->from_table = expect_ident();
        if (accept_kw("JOIN")) {
            stmt->join.present = true;
            stmt->join.table = expect_ident();
            expect_kw("ON");
            stmt->join.left = parse_colref();
            parse_op(); // only '=' joins are meaningful; operator consumed
            stmt->join.right = parse_colref();
        }
        stmt->where = parse_where();
        accept_punct(";");
        return stmt;
    }

    std::unique_ptr<Statement> parse_delete() {
        expect_kw("DELETE"); expect_kw("FROM");
        auto stmt = std::make_unique<DeleteStmt>();
        stmt->table = expect_ident();
        stmt->where = parse_where();
        accept_punct(";");
        return stmt;
    }

    std::vector<Token> toks_;
    size_t             pos_{0};
};

} // namespace

std::unique_ptr<Statement> ParseSQL(const std::string &sql) {
    Parser p(Lex(sql));
    return p.parse();
}

} // namespace minidb
