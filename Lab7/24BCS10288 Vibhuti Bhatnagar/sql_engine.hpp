// sql_engine.hpp — ADBMS Lab 7 / 24BCS10288 Vibhuti Bhatnagar
//
// A header-only mini SQL front end built around Dijkstra's shunting-yard
// algorithm. The pipeline mirrors a real database front end at three stages:
//
//     sql_text  --lex-->     [Token]
//               --parse-->   Query  (WHERE compiled to RPN)
//               --run-->     Relation
//
// Supports SELECT  (col_list | * | COUNT(*))
//          FROM    table
//          WHERE   <predicate>            -- compiled to RPN once
//          ORDER BY col [ASC|DESC]
//          LIMIT n
//
// Operators inside WHERE: = != < <= > >= LIKE AND OR NOT and parentheses.
// LIKE supports the SQL '%' wildcard (matches zero or more characters).
// String literals use single quotes; doubling a quote escapes it ('don''t').
// Columns can be integers or strings (std::variant). Aggregation is
// limited to COUNT(*); no GROUP BY (out of scope for the lab).

#pragma once

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace adbms::sql {

// ============================================================================
// 1. data model
// ============================================================================

using Cell     = std::variant<std::int64_t, std::string>;
using Row      = std::vector<Cell>;

struct Relation {
    std::string              name;
    std::vector<std::string> columns;
    std::vector<Row>         rows;

    int find_column(const std::string& c) const {
        for (std::size_t i = 0; i < columns.size(); ++i)
            if (columns[i] == c) return static_cast<int>(i);
        return -1;
    }
};

// ============================================================================
// 2. tokens
// ============================================================================

enum class Kind {
    // literals + identifiers
    Ident, IntLit, StrLit,
    // operator with text — comparisons, AND/OR/NOT, LIKE
    Op,
    // punctuation
    LParen, RParen, Comma, Star,
    // keywords
    KwSelect, KwFrom, KwWhere, KwOrder, KwBy, KwAsc, KwDesc, KwLimit, KwCount,
    // sentinel
    End
};

struct Token {
    Kind         kind;
    std::string  text;
    std::int64_t ivalue = 0;        // valid for Kind::IntLit only
};

// ============================================================================
// 3. lexer
// ============================================================================

inline bool is_word_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Case-insensitive keyword match.
inline bool eq_ic(const std::string& a, const char* b) {
    std::size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return i == a.size() && b[i] == '\0';
}

inline std::vector<Token> lex(const std::string& src) {
    std::vector<Token> out;
    const std::size_t  n = src.size();
    std::size_t        i = 0;
    while (i < n) {
        char c = src[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

        // punctuation
        if (c == '(') { out.push_back({Kind::LParen, "(", 0}); ++i; continue; }
        if (c == ')') { out.push_back({Kind::RParen, ")", 0}); ++i; continue; }
        if (c == ',') { out.push_back({Kind::Comma,  ",", 0}); ++i; continue; }
        if (c == '*') { out.push_back({Kind::Star,   "*", 0}); ++i; continue; }

        // numeric literal
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '-' && i + 1 < n && std::isdigit(static_cast<unsigned char>(src[i+1])))) {
            std::size_t j = (c == '-') ? i + 1 : i;
            while (j < n && std::isdigit(static_cast<unsigned char>(src[j]))) ++j;
            std::string lit = src.substr(i, j - i);
            out.push_back({Kind::IntLit, lit, std::stoll(lit)});
            i = j;
            continue;
        }

        // string literal — '...' with '' as escape
        if (c == '\'') {
            std::string s;
            ++i;                                            // skip opening quote
            while (i < n) {
                if (src[i] == '\'') {
                    if (i + 1 < n && src[i+1] == '\'') {     // doubled quote -> literal '
                        s.push_back('\'');
                        i += 2;
                        continue;
                    }
                    ++i;                                     // closing quote
                    break;
                }
                s.push_back(src[i++]);
            }
            out.push_back({Kind::StrLit, std::move(s), 0});
            continue;
        }

        // comparison operators
        if (c == '=') { out.push_back({Kind::Op, "=", 0}); ++i; continue; }
        if (c == '<' || c == '>' || c == '!') {
            std::string sym(1, c);
            if (i + 1 < n && src[i+1] == '=') { sym.push_back('='); ++i; }
            ++i;
            out.push_back({Kind::Op, sym, 0});
            continue;
        }

        // identifier or keyword
        if (is_word_char(c)) {
            std::size_t j = i;
            while (j < n && is_word_char(src[j])) ++j;
            std::string word = src.substr(i, j - i);
            i = j;
            if      (eq_ic(word, "select")) out.push_back({Kind::KwSelect, word, 0});
            else if (eq_ic(word, "from"))   out.push_back({Kind::KwFrom,   word, 0});
            else if (eq_ic(word, "where"))  out.push_back({Kind::KwWhere,  word, 0});
            else if (eq_ic(word, "order"))  out.push_back({Kind::KwOrder,  word, 0});
            else if (eq_ic(word, "by"))     out.push_back({Kind::KwBy,     word, 0});
            else if (eq_ic(word, "asc"))    out.push_back({Kind::KwAsc,    word, 0});
            else if (eq_ic(word, "desc"))   out.push_back({Kind::KwDesc,   word, 0});
            else if (eq_ic(word, "limit"))  out.push_back({Kind::KwLimit,  word, 0});
            else if (eq_ic(word, "count"))  out.push_back({Kind::KwCount,  word, 0});
            else if (eq_ic(word, "and") || eq_ic(word, "or") ||
                     eq_ic(word, "not") || eq_ic(word, "like")) {
                // canonicalise to upper-case for the RPN trace
                for (char& ch : word) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                out.push_back({Kind::Op, word, 0});
            } else {
                out.push_back({Kind::Ident, word, 0});
            }
            continue;
        }

        throw std::runtime_error("sql_engine: unexpected character at byte " +
                                 std::to_string(i));
    }
    out.push_back({Kind::End, "", 0});
    return out;
}

// ============================================================================
// 4. shunting-yard — Dijkstra's classic infix -> postfix conversion
// ============================================================================

// Precedence (higher binds tighter) and associativity for the operators we
// recognise. The defaults below give the SQL-standard order
//     comparisons  >  NOT  >  AND  >  OR
// which matches how queries like `NOT a = 1 OR b = 2` are universally read.
inline int precedence(const std::string& op) {
    if (op == "=" || op == "!=" ||
        op == "<" || op == "<=" || op == ">" || op == ">=" ||
        op == "LIKE")                return 4;
    if (op == "NOT")                 return 3;
    if (op == "AND")                 return 2;
    if (op == "OR")                  return 1;
    return 0;
}
inline bool is_right_assoc(const std::string& op) { return op == "NOT"; }
inline bool is_unary(const std::string& op)       { return op == "NOT"; }

inline std::vector<Token> to_rpn(const std::vector<Token>& infix) {
    std::vector<Token> output;
    std::stack<Token>  ops;

    for (const Token& tok : infix) {
        switch (tok.kind) {
            case Kind::Ident:
            case Kind::IntLit:
            case Kind::StrLit:
                output.push_back(tok);
                break;

            case Kind::Op: {
                while (!ops.empty() && ops.top().kind == Kind::Op) {
                    const Token& top = ops.top();
                    const int pt = precedence(top.text);
                    const int pn = precedence(tok.text);
                    if (pt > pn || (pt == pn && !is_right_assoc(tok.text))) {
                        output.push_back(top);
                        ops.pop();
                    } else break;
                }
                ops.push(tok);
                break;
            }

            case Kind::LParen:
                ops.push(tok);
                break;

            case Kind::RParen:
                while (!ops.empty() && ops.top().kind != Kind::LParen) {
                    output.push_back(ops.top());
                    ops.pop();
                }
                if (ops.empty())
                    throw std::runtime_error("sql_engine: unmatched ')' in WHERE");
                ops.pop();                          // discard the LParen
                break;

            default:
                // anything else (comma, star, keyword) shouldn't appear inside
                // a WHERE expression slice; treat the slice as ending here.
                break;
        }
    }
    while (!ops.empty()) {
        if (ops.top().kind == Kind::LParen)
            throw std::runtime_error("sql_engine: unmatched '(' in WHERE");
        output.push_back(ops.top());
        ops.pop();
    }
    return output;
}

inline std::string rpn_trace(const std::vector<Token>& rpn) {
    std::ostringstream ss;
    bool first = true;
    for (const Token& t : rpn) {
        if (!first) ss << ' ';
        first = false;
        if      (t.kind == Kind::StrLit) ss << "'" << t.text << "'";
        else                              ss << t.text;
    }
    return ss.str();
}

// ============================================================================
// 5. RPN evaluator
// ============================================================================

// A node on the eval stack — either a column reference, an int literal, a
// string literal, or a materialised boolean from a comparison.
struct Eval {
    enum class Type { Int, Str, Bool } type;
    std::int64_t i = 0;
    std::string  s;
};

inline Eval resolve(const Token& t, const Relation& schema, const Row& row) {
    Eval e;
    if (t.kind == Kind::IntLit) { e.type = Eval::Type::Int; e.i = t.ivalue; return e; }
    if (t.kind == Kind::StrLit) { e.type = Eval::Type::Str; e.s = t.text;    return e; }
    if (t.kind == Kind::Ident) {
        int col = schema.find_column(t.text);
        if (col < 0)
            throw std::runtime_error("sql_engine: unknown column '" + t.text + "'");
        const Cell& cell = row[col];
        if (std::holds_alternative<std::int64_t>(cell)) {
            e.type = Eval::Type::Int; e.i = std::get<std::int64_t>(cell);
        } else {
            e.type = Eval::Type::Str; e.s = std::get<std::string>(cell);
        }
        return e;
    }
    throw std::runtime_error("sql_engine: bad operand in WHERE");
}

// SQL LIKE: only '%' wildcard supported (matches any sequence, including
// empty). Implemented with a simple two-pointer + back-tracking algorithm.
inline bool like_match(const std::string& s, const std::string& p) {
    std::size_t si = 0, pi = 0, star = std::string::npos, mark = 0;
    while (si < s.size()) {
        if (pi < p.size() && p[pi] == '%') {
            star = pi++;
            mark = si;
        } else if (pi < p.size() && p[pi] == s[si]) {
            ++pi; ++si;
        } else if (star != std::string::npos) {
            pi = star + 1;
            si = ++mark;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == '%') ++pi;
    return pi == p.size();
}

inline int compare(const Eval& a, const Eval& b) {
    if (a.type == Eval::Type::Int && b.type == Eval::Type::Int)
        return (a.i < b.i) ? -1 : (a.i > b.i ? 1 : 0);
    if (a.type == Eval::Type::Str && b.type == Eval::Type::Str)
        return a.s.compare(b.s) < 0 ? -1 : (a.s.compare(b.s) > 0 ? 1 : 0);
    throw std::runtime_error("sql_engine: type mismatch in comparison");
}

inline bool run_rpn(const std::vector<Token>& rpn,
                    const Relation& schema, const Row& row) {
    std::vector<Eval> stk;

    auto pop = [&]() {
        if (stk.empty()) throw std::runtime_error("sql_engine: stack underflow");
        Eval e = std::move(stk.back()); stk.pop_back(); return e;
    };
    auto push_bool = [&](bool b) {
        Eval e; e.type = Eval::Type::Bool; e.i = b ? 1 : 0; stk.push_back(std::move(e));
    };
    auto to_bool = [](const Eval& e) {
        if (e.type == Eval::Type::Bool) return e.i != 0;
        throw std::runtime_error("sql_engine: expected boolean operand");
    };

    for (const Token& t : rpn) {
        if (t.kind != Kind::Op) {
            stk.push_back(resolve(t, schema, row));
            continue;
        }
        const std::string& op = t.text;
        if (op == "NOT") {
            bool b = to_bool(pop());
            push_bool(!b);
            continue;
        }
        if (op == "AND" || op == "OR") {
            bool r = to_bool(pop());
            bool l = to_bool(pop());
            push_bool(op == "AND" ? (l && r) : (l || r));
            continue;
        }
        // comparison
        Eval r = pop();
        Eval l = pop();
        if (op == "LIKE") {
            if (l.type != Eval::Type::Str || r.type != Eval::Type::Str)
                throw std::runtime_error("sql_engine: LIKE expects two strings");
            push_bool(like_match(l.s, r.s));
            continue;
        }
        int c = compare(l, r);
        if      (op == "=")  push_bool(c == 0);
        else if (op == "!=") push_bool(c != 0);
        else if (op == "<")  push_bool(c <  0);
        else if (op == "<=") push_bool(c <= 0);
        else if (op == ">")  push_bool(c >  0);
        else if (op == ">=") push_bool(c >= 0);
        else throw std::runtime_error("sql_engine: unknown operator '" + op + "'");
    }
    if (stk.size() != 1)
        throw std::runtime_error("sql_engine: WHERE did not reduce to one value");
    return to_bool(stk.back());
}

// ============================================================================
// 6. parser — recursive-descent for the outer SQL skeleton; WHERE is sliced
//    and handed to the shunting-yard converter as a unit
// ============================================================================

struct Query {
    std::vector<std::string> projection;     // empty => SELECT *
    bool                     is_count_star = false;
    std::string              table;
    std::vector<Token>       where_rpn;
    std::string              order_col;       // "" => no ORDER BY
    bool                     order_desc = false;
    std::int64_t             limit = -1;
};

inline Query parse(const std::string& sql) {
    std::vector<Token> tk = lex(sql);
    std::size_t p = 0;

    auto peek = [&]() -> const Token& { return tk[p]; };
    auto eat = [&](Kind k, const char* what) -> const Token& {
        if (tk[p].kind != k)
            throw std::runtime_error(std::string("sql_engine: expected ") + what +
                                     ", got '" + tk[p].text + "'");
        return tk[p++];
    };

    eat(Kind::KwSelect, "SELECT");

    Query q;
    if (peek().kind == Kind::Star) {
        ++p;                                // SELECT *
    } else if (peek().kind == Kind::KwCount) {
        ++p; eat(Kind::LParen, "("); eat(Kind::Star, "*"); eat(Kind::RParen, ")");
        q.is_count_star = true;
    } else {
        q.projection.push_back(eat(Kind::Ident, "column name").text);
        while (peek().kind == Kind::Comma) {
            ++p;
            q.projection.push_back(eat(Kind::Ident, "column name").text);
        }
    }

    eat(Kind::KwFrom, "FROM");
    q.table = eat(Kind::Ident, "table name").text;

    if (peek().kind == Kind::KwWhere) {
        ++p;
        // Collect tokens until we hit ORDER / LIMIT / End.
        std::vector<Token> slice;
        while (peek().kind != Kind::KwOrder &&
               peek().kind != Kind::KwLimit &&
               peek().kind != Kind::End) {
            slice.push_back(tk[p++]);
        }
        q.where_rpn = to_rpn(slice);
    }

    if (peek().kind == Kind::KwOrder) {
        ++p; eat(Kind::KwBy, "BY");
        q.order_col = eat(Kind::Ident, "column for ORDER BY").text;
        if (peek().kind == Kind::KwAsc)  { ++p; }
        else if (peek().kind == Kind::KwDesc) { ++p; q.order_desc = true; }
    }

    if (peek().kind == Kind::KwLimit) {
        ++p;
        q.limit = eat(Kind::IntLit, "integer after LIMIT").ivalue;
    }
    eat(Kind::End, "end of statement");
    return q;
}

// ============================================================================
// 7. execute
// ============================================================================

inline Relation run(const Query& q, const Relation& src) {
    // 7a. determine result columns
    Relation out;
    out.name = q.table;

    if (q.is_count_star) {
        out.columns = {"count"};
    } else if (q.projection.empty()) {
        out.columns = src.columns;
    } else {
        out.columns = q.projection;
        for (const std::string& c : out.columns)
            if (src.find_column(c) < 0)
                throw std::runtime_error("sql_engine: unknown column '" + c + "'");
    }

    // 7b. filter
    std::vector<const Row*> kept;
    kept.reserve(src.rows.size());
    for (const Row& row : src.rows) {
        if (!q.where_rpn.empty() && !run_rpn(q.where_rpn, src, row)) continue;
        kept.push_back(&row);
    }

    // 7c. order
    if (!q.order_col.empty()) {
        int col = src.find_column(q.order_col);
        if (col < 0)
            throw std::runtime_error("sql_engine: ORDER BY references unknown column '" + q.order_col + "'");
        std::sort(kept.begin(), kept.end(), [&](const Row* a, const Row* b) {
            const Cell& av = (*a)[col];
            const Cell& bv = (*b)[col];
            bool less;
            if (std::holds_alternative<std::int64_t>(av))
                less = std::get<std::int64_t>(av) < std::get<std::int64_t>(bv);
            else
                less = std::get<std::string>(av) <  std::get<std::string>(bv);
            return q.order_desc ? !less && av != bv : less;
        });
    }

    // 7d. limit
    if (q.limit >= 0 && static_cast<std::size_t>(q.limit) < kept.size())
        kept.resize(static_cast<std::size_t>(q.limit));

    // 7e. project / aggregate
    if (q.is_count_star) {
        out.rows.push_back(Row{ Cell{ static_cast<std::int64_t>(kept.size()) } });
        return out;
    }
    if (q.projection.empty()) {
        for (const Row* r : kept) out.rows.push_back(*r);
    } else {
        std::vector<int> idx;
        idx.reserve(q.projection.size());
        for (const std::string& c : q.projection) idx.push_back(src.find_column(c));
        for (const Row* r : kept) {
            Row newrow;
            newrow.reserve(idx.size());
            for (int j : idx) newrow.push_back((*r)[j]);
            out.rows.push_back(std::move(newrow));
        }
    }
    return out;
}

// ============================================================================
// 8. pretty printer — column-aligned table
// ============================================================================

inline std::string cell_to_str(const Cell& c) {
    if (std::holds_alternative<std::int64_t>(c))
        return std::to_string(std::get<std::int64_t>(c));
    return std::get<std::string>(c);
}

inline void print(const Relation& r, std::ostream& os = std::cout) {
    if (r.columns.empty()) { os << "(empty)\n"; return; }
    std::vector<std::size_t> w(r.columns.size());
    for (std::size_t i = 0; i < r.columns.size(); ++i) w[i] = r.columns[i].size();
    for (const Row& row : r.rows)
        for (std::size_t i = 0; i < row.size(); ++i)
            w[i] = std::max(w[i], cell_to_str(row[i]).size());

    auto sep = [&]() {
        for (std::size_t i = 0; i < w.size(); ++i)
            os << '+' << std::string(w[i] + 2, '-');
        os << "+\n";
    };
    sep();
    for (std::size_t i = 0; i < r.columns.size(); ++i)
        os << "| " << std::left << std::setw(static_cast<int>(w[i])) << r.columns[i] << ' ';
    os << "|\n"; sep();
    for (const Row& row : r.rows) {
        for (std::size_t i = 0; i < row.size(); ++i)
            os << "| " << std::left << std::setw(static_cast<int>(w[i])) << cell_to_str(row[i]) << ' ';
        os << "|\n";
    }
    sep();
    os << "(" << r.rows.size() << " row" << (r.rows.size() == 1 ? "" : "s") << ")\n";
}

}  // namespace adbms::sql
