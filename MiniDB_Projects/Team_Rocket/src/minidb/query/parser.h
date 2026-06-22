#pragma once

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

#include "../types.h"

namespace minidb {

enum class StmtKind { CreateTable, Insert, Select, Delete, Begin, Commit, Abort, Crash, Unknown };

struct Predicate {
    bool present = false;
    std::string col;
    std::string op;
    Value val;
};

struct Statement {
    StmtKind kind = StmtKind::Unknown;
    std::string error;

    std::string table;
    Schema schema;                 // CreateTable
    std::vector<Value> values;     // Insert

    std::vector<std::string> select_cols;  // Select projection ({"*"} for all)
    bool has_join = false;
    std::string join_table, left_on, right_on;
    Predicate where;
};

// A tiny hand-written SQL parser. It supports the statement shapes MiniDB
// needs: CREATE TABLE, INSERT, SELECT (projection / single inner join / WHERE),
// DELETE, and transaction control.
class Parser {
public:
    static Statement parse(const std::string& sql) {
        Parser p(sql);
        return p.statement();
    }

private:
    struct Token {
        std::string text;
        bool str = false;  // true if it came from a quoted literal
    };

    std::vector<Token> toks_;
    size_t pos_ = 0;

    explicit Parser(const std::string& sql) { lex(sql); }

    void lex(const std::string& s) {
        size_t i = 0;
        auto is_sym = [](char c) { return c == '(' || c == ')' || c == ',' || c == '*'; };
        while (i < s.size()) {
            char c = s[i];
            if (std::isspace(static_cast<unsigned char>(c))) {
                ++i;
                continue;
            }
            if (c == '\'') {
                ++i;
                std::string v;
                while (i < s.size() && s[i] != '\'') v += s[i++];
                if (i < s.size()) ++i;
                toks_.push_back({v, true});
                continue;
            }
            if (is_sym(c)) {
                toks_.push_back({std::string(1, c), false});
                ++i;
                continue;
            }
            if (c == '=' || c == '<' || c == '>' || c == '!') {
                std::string op(1, c);
                ++i;
                if (i < s.size() && s[i] == '=') {
                    op += '=';
                    ++i;
                }
                toks_.push_back({op, false});
                continue;
            }
            std::string w;
            while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) &&
                   !is_sym(s[i]) && s[i] != '=' && s[i] != '<' && s[i] != '>' && s[i] != '!' &&
                   s[i] != '\'')
                w += s[i++];
            toks_.push_back({w, false});
        }
    }

    bool done() const { return pos_ >= toks_.size(); }
    const Token& peek() const {
        static Token empty;
        return done() ? empty : toks_[pos_];
    }
    Token next() { return done() ? Token{} : toks_[pos_++]; }

    static std::string up(const std::string& s) {
        std::string r = s;
        for (char& c : r) c = std::toupper(static_cast<unsigned char>(c));
        return r;
    }
    bool kw(const std::string& k) const { return up(peek().text) == k && !peek().str; }
    bool eat_kw(const std::string& k) {
        if (kw(k)) {
            ++pos_;
            return true;
        }
        return false;
    }

    static Value literal(const Token& t) {
        if (t.str) return Value::make_text(t.text);
        char* end = nullptr;
        long long v = std::strtoll(t.text.c_str(), &end, 10);
        if (end && *end == '\0' && !t.text.empty()) return Value::make_int(v);
        return Value::make_text(t.text);
    }

    static Type type_of(const std::string& t) {
        std::string u = up(t);
        if (u == "INT" || u == "INTEGER" || u == "BIGINT") return Type::Int;
        return Type::Text;  // TEXT / VARCHAR / CHAR / STRING
    }

    Statement statement() {
        Statement st;
        if (done()) return st;
        if (eat_kw("CREATE")) return create_table(st);
        if (eat_kw("INSERT")) return insert(st);
        if (eat_kw("SELECT")) return select(st);
        if (eat_kw("DELETE")) return del(st);
        if (eat_kw("BEGIN") || eat_kw("START")) {
            eat_kw("TRANSACTION");
            st.kind = StmtKind::Begin;
            return st;
        }
        if (eat_kw("COMMIT")) {
            st.kind = StmtKind::Commit;
            return st;
        }
        if (eat_kw("ABORT") || eat_kw("ROLLBACK")) {
            st.kind = StmtKind::Abort;
            return st;
        }
        if (eat_kw("CRASH")) {
            st.kind = StmtKind::Crash;
            return st;
        }
        st.error = "unrecognised statement";
        return st;
    }

    Statement create_table(Statement st) {
        st.kind = StmtKind::CreateTable;
        if (!eat_kw("TABLE")) {
            st.error = "expected TABLE";
            return st;
        }
        st.table = next().text;
        if (next().text != "(") {
            st.error = "expected (";
            return st;
        }
        while (!done() && peek().text != ")") {
            std::string col = next().text;
            std::string ty = next().text;
            st.schema.push_back({col, type_of(ty)});
            if (peek().text == "(") {  // skip a size like VARCHAR(20)
                while (!done() && next().text != ")") {
                }
            }
            if (peek().text == ",") ++pos_;
        }
        eat(")");
        return st;
    }

    Statement insert(Statement st) {
        st.kind = StmtKind::Insert;
        eat_kw("INTO");
        st.table = next().text;
        eat_kw("VALUES");
        if (next().text != "(") {
            st.error = "expected (";
            return st;
        }
        while (!done() && peek().text != ")") {
            st.values.push_back(literal(next()));
            if (peek().text == ",") ++pos_;
        }
        eat(")");
        return st;
    }

    Statement select(Statement st) {
        st.kind = StmtKind::Select;
        while (!done() && !kw("FROM")) {
            st.select_cols.push_back(next().text);
            if (peek().text == ",") ++pos_;
        }
        if (!eat_kw("FROM")) {
            st.error = "expected FROM";
            return st;
        }
        st.table = next().text;
        if (eat_kw("JOIN")) {
            st.has_join = true;
            st.join_table = next().text;
            if (!eat_kw("ON")) {
                st.error = "expected ON";
                return st;
            }
            st.left_on = next().text;
            next();  // '='
            st.right_on = next().text;
        }
        if (eat_kw("WHERE")) st.where = predicate(st);
        return st;
    }

    Statement del(Statement st) {
        st.kind = StmtKind::Delete;
        eat_kw("FROM");
        st.table = next().text;
        if (eat_kw("WHERE")) st.where = predicate(st);
        return st;
    }

    Predicate predicate(Statement& st) {
        Predicate p;
        p.present = true;
        p.col = next().text;
        p.op = next().text;
        if (done()) {
            st.error = "incomplete WHERE clause";
            return p;
        }
        p.val = literal(next());
        return p;
    }

    void eat(const std::string& t) {
        if (peek().text == t) ++pos_;
    }
};

}  // namespace minidb
