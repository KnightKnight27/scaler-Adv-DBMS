#include "parser.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace minidb {

// ── Tokenizer ─────────────────────────────────────────────────────────────────
// Splits SQL into tokens: words, integer literals, quoted strings, punctuation.
// We keep it simple: one character at a time, no regex.

struct Token {
    enum Kind { WORD, INT_LIT, STR_LIT, PUNCT, END } kind;
    std::string text;
    int         ival = 0;
};

static std::vector<Token> tokenize(const std::string& sql) {
    std::vector<Token> toks;
    size_t i = 0;

    while (i < sql.size()) {
        char c = sql[i];

        if (std::isspace(c)) { i++; continue; }

        if (c == '\'') {
            // Single-quoted string literal.
            std::string s;
            i++;
            while (i < sql.size() && sql[i] != '\'') s += sql[i++];
            if (i < sql.size()) i++;  // consume closing quote
            toks.push_back({Token::STR_LIT, s});
            continue;
        }

        if (std::isdigit(c) || (c == '-' && i + 1 < sql.size() && std::isdigit(sql[i+1]))) {
            std::string n;
            if (c == '-') { n += c; i++; }
            while (i < sql.size() && std::isdigit(sql[i])) n += sql[i++];
            toks.push_back({Token::INT_LIT, n, std::stoi(n)});
            continue;
        }

        if (std::isalpha(c) || c == '_') {
            std::string w;
            while (i < sql.size() && (std::isalnum(sql[i]) || sql[i] == '_' || sql[i] == '.'))
                w += sql[i++];
            // Normalize keywords to uppercase for case-insensitive matching.
            std::string upper = w;
            for (char& ch : upper) ch = static_cast<char>(std::toupper(ch));
            toks.push_back({Token::WORD, upper});
            // Keep original text as well for identifiers (column / table names).
            // We store the uppercased version; since our identifiers are
            // case-insensitive anyway this is fine.
            continue;
        }

        // Single-character punctuation: ( ) , = ! < > *
        toks.push_back({Token::PUNCT, std::string(1, c)});
        i++;
    }

    toks.push_back({Token::END, ""});
    return toks;
}

// ── Parser state ──────────────────────────────────────────────────────────────

struct Parser {
    std::vector<Token> tokens;
    int pos = 0;

    Token& cur()  { return tokens[pos]; }
    Token& peek(int offset = 1) { return tokens[pos + offset]; }

    void advance() { if (cur().kind != Token::END) pos++; }

    // Consume a token if it matches expected text; throw otherwise.
    void expect(const std::string& text) {
        if (cur().text != text)
            throw ParseError("Expected '" + text + "', got '" + cur().text + "'");
        advance();
    }

    std::string word() {
        if (cur().kind != Token::WORD)
            throw ParseError("Expected identifier, got '" + cur().text + "'");
        std::string w = cur().text;
        advance();
        return w;
    }

    bool at(const std::string& text) { return cur().text == text; }

    bool try_consume(const std::string& text) {
        if (cur().text == text) { advance(); return true; }
        return false;
    }

    // Parse a single value literal (INT or VARCHAR).
    Value value_literal() {
        if (cur().kind == Token::INT_LIT) {
            int v = cur().ival; advance();
            return Value::make_int(v);
        }
        if (cur().kind == Token::STR_LIT) {
            std::string v = cur().text; advance();
            return Value::make_str(std::move(v));
        }
        throw ParseError("Expected value literal, got '" + cur().text + "'");
    }

    // Parse a comparison operator.
    Op parse_op() {
        std::string s = cur().text;
        advance();
        if (s == "=")  return Op::EQ;
        if (s == "!=") return Op::NE;
        if (s == "<>") return Op::NE;
        if (s == "<")  {
            if (cur().text == "=") { advance(); return Op::LE; }
            return Op::LT;
        }
        if (s == ">")  {
            if (cur().text == "=") { advance(); return Op::GE; }
            return Op::GT;
        }
        if (s == "!" && cur().text == "=") { advance(); return Op::NE; }
        throw ParseError("Expected comparison operator, got '" + s + "'");
    }

    // Parse one WHERE condition: col OP literal
    Cond parse_cond() {
        Cond c;
        c.col = cur().text; advance();     // may be "table.col"
        // Handle two-char ops like != <= >=
        if ((cur().text == "!" || cur().text == "<" || cur().text == ">") &&
            peek().text == "=") {
            std::string combined = cur().text + "=";
            advance(); advance();
            if (combined == "!=") c.op = Op::NE;
            else if (combined == "<=") c.op = Op::LE;
            else c.op = Op::GE;
        } else {
            c.op = parse_op();
        }
        c.val = value_literal();
        return c;
    }

    // Parse a comma-separated WHERE clause (just AND for simplicity).
    std::vector<Cond> parse_where() {
        std::vector<Cond> conds;
        if (!try_consume("WHERE")) return conds;
        conds.push_back(parse_cond());
        while (try_consume("AND")) conds.push_back(parse_cond());
        return conds;
    }
};

// ── Statement parsers ─────────────────────────────────────────────────────────

static Stmt parse_create(Parser& p) {
    Stmt s; s.kind = Kind::CREATE;
    s.table = p.word();
    p.expect("(");
    while (true) {
        ColDef cd;
        cd.name = p.word();
        if      (p.cur().text == "INT")     { cd.type = Type::INT;     p.advance(); }
        else if (p.cur().text == "VARCHAR") { cd.type = Type::VARCHAR; p.advance(); }
        else throw ParseError("Unknown type: " + p.cur().text);

        if (p.try_consume("PRIMARY")) {
            p.expect("KEY");
            cd.pk = true;
        }
        s.cols.push_back(cd);
        if (!p.try_consume(",")) break;
    }
    p.expect(")");
    return s;
}

static Stmt parse_insert(Parser& p) {
    Stmt s; s.kind = Kind::INSERT;
    p.expect("INTO");
    s.table = p.word();
    p.expect("VALUES");
    p.expect("(");
    while (true) {
        s.values.push_back(p.value_literal());
        if (!p.try_consume(",")) break;
    }
    p.expect(")");
    return s;
}

static Stmt parse_select(Parser& p) {
    Stmt s; s.kind = Kind::SELECT;

    // Column list or *
    if (p.try_consume("*")) {
        s.star = true;
    } else {
        do {
            s.sel_cols.push_back(p.cur().text);
            p.advance();
        } while (p.try_consume(","));
    }

    p.expect("FROM");
    s.table = p.word();

    // Optional JOIN
    if (p.try_consume("JOIN")) {
        s.has_join   = true;
        s.join_table = p.word();
        p.expect("ON");
        // ON t1.col = t2.col
        s.join_left  = p.cur().text; p.advance();
        p.expect("=");
        s.join_right = p.cur().text; p.advance();
    }

    s.where = p.parse_where();
    return s;
}

static Stmt parse_delete(Parser& p) {
    Stmt s; s.kind = Kind::DELETE;
    p.expect("FROM");
    s.table = p.word();
    s.where = p.parse_where();
    return s;
}

// ── Entry point ───────────────────────────────────────────────────────────────

Stmt parse(const std::string& sql) {
    Parser p;
    p.tokens = tokenize(sql);

    Stmt s;

    // EXPLAIN prefix
    bool explain = p.try_consume("EXPLAIN");

    std::string kw = p.word();

    if      (kw == "CREATE") { p.expect("TABLE"); s = parse_create(p); }
    else if (kw == "INSERT") { s = parse_insert(p); }
    else if (kw == "SELECT") { s = parse_select(p); s.explain = explain; }
    else if (kw == "DELETE") { s = parse_delete(p); }
    else if (kw == "BEGIN")  { s.kind = Kind::BEGIN; }
    else if (kw == "COMMIT") { s.kind = Kind::COMMIT; }
    else if (kw == "ABORT")  { s.kind = Kind::ABORT; }
    else throw ParseError("Unknown statement: " + kw);

    return s;
}

} // namespace minidb
