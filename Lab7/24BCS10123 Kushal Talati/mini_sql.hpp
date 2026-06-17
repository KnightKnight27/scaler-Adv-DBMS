// Lab 7 — Shunting-Yard + a tiny SQL SELECT engine (header-only)
// 24BCS10123  Kushal Talati
//
// kt::QueryEngine runs a SELECT against an in-memory kt::Relation. The path a
// query takes mirrors a real database front end:
//
//     SQL text  --lex-->        tokens
//               --compile-->    Query   (the WHERE clause is kept as postfix)
//               --run-->        result Relation  (filter, project, sort, cap)
//
// The WHERE clause is rewritten from infix to postfix (RPN) exactly once, with
// Dijkstra's shunting-yard, so operator precedence and parentheses are folded
// into the token order; per-row evaluation is then a single stack pass.
//
// Cells are typed: every value is either a 64-bit integer or text
// (std::variant). WHERE understands = != < <= > >= AND OR NOT and parentheses.
//
// This is deliberately header-only — the whole engine is small enough that one
// translation unit (runner.cpp) includes it directly, the way my Lab-6 B-tree
// index is also a single .hpp.

#ifndef KT_LAB7_MINI_SQL_HPP
#define KT_LAB7_MINI_SQL_HPP

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <ostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace kt {

// ---------------------------------------------------------------------------
// Typed value model
// ---------------------------------------------------------------------------

// A single cell is an integer or a text string. Alternative 0 is the integer.
using Cell = std::variant<long long, std::string>;

inline bool is_int(const Cell& c)  { return c.index() == 0; }
inline bool is_text(const Cell& c) { return c.index() == 1; }

// Render a cell to text for the table printer.
inline std::string cell_text(const Cell& c) {
    return is_int(c) ? std::to_string(std::get<long long>(c)) : std::get<std::string>(c);
}

// Truth of a cell used in a boolean context: nonzero int / non-empty string.
inline bool cell_true(const Cell& c) {
    return is_int(c) ? (std::get<long long>(c) != 0) : !std::get<std::string>(c).empty();
}

// Three-way order. Two ints compare numerically, two strings lexically; a
// number-vs-text pair is unorderable and reported through `comparable`.
inline int cell_cmp(const Cell& a, const Cell& b, bool& comparable) {
    comparable = true;
    if (is_int(a) && is_int(b)) {
        long long x = std::get<long long>(a), y = std::get<long long>(b);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    if (is_text(a) && is_text(b)) {
        const auto& x = std::get<std::string>(a);
        const auto& y = std::get<std::string>(b);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    comparable = false;
    return 0;
}

struct Tuple {
    std::vector<Cell> fields;   // positional, aligned with Relation::heading
};

struct Relation {
    std::string              name;
    std::vector<std::string> heading;   // column names, in order
    std::vector<Tuple>       tuples;

    // Index of a column, or -1 when it is not part of the heading.
    int column_of(const std::string& c) const {
        for (std::size_t i = 0; i < heading.size(); ++i)
            if (heading[i] == c) return static_cast<int>(i);
        return -1;
    }
};

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------

enum class Lexeme {
    Name, Number, Text,                     // operands
    Operator,                               // = != < <= > >= AND OR NOT
    Open, Close, Comma, Star,               // ( ) , *
    KwSelect, KwFrom, KwWhere,
    KwOrder, KwBy, KwAsc, KwDesc, KwLimit,
    Eof
};

struct Token {
    Lexeme      kind;
    std::string spelling;    // column name, operator symbol, or raw string body
    long long   number = 0;  // meaningful only when kind == Number
};

// ---------------------------------------------------------------------------
// Compiled query
// ---------------------------------------------------------------------------

struct Query {
    std::vector<std::string> select;     // empty == SELECT *
    std::string              from;
    std::vector<Token>       filter;     // WHERE in postfix; empty == no filter
    std::string              sort_key;   // "" == no ORDER BY
    bool                     sort_desc = false;
    long long                cap = -1;   // -1 == no LIMIT
};

// ---------------------------------------------------------------------------
// The engine
// ---------------------------------------------------------------------------

class QueryEngine {
public:
    // Whole-pipeline convenience: lex -> compile -> run.
    Relation run(const std::string& sql, const Relation& src) const {
        return run(compile(sql), src);
    }

    // --- stages, exposed individually so the demo can trace each one ---

    std::vector<Token> lex(const std::string& sql) const;

    // Infix WHERE tokens -> postfix (RPN). Static: it touches no engine state.
    static std::vector<Token> to_postfix(const std::vector<Token>& infix);

    Query compile(const std::string& sql) const;

    // Evaluate a compiled (postfix) predicate against one tuple.
    static bool matches(const std::vector<Token>& postfix,
                        const Relation& schema, const Tuple& row);

    Relation run(const Query& q, const Relation& src) const;

    // Flatten a postfix token list back to text (tracing / demo output).
    static std::string postfix_text(const std::vector<Token>& postfix);

    static void render(const Relation& r, std::ostream& os);

private:
    // Operator descriptor. Higher `power` binds tighter.
    struct OpRank { int power; bool unary; bool right_assoc; };
    static OpRank rank_of(const std::string& sym);

    static Cell operand_of(const Token& t, const Relation& schema, const Tuple& row);

    // Lexer character classes.
    static bool starts_word(char c) {
        return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
    }
    static bool in_word(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    }
    static std::string upcase(std::string s) {
        for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }
};

// ===========================================================================
// Operator table
// ===========================================================================

inline QueryEngine::OpRank QueryEngine::rank_of(const std::string& sym) {
    if (sym == "=" || sym == "!=" || sym == "<" ||
        sym == "<=" || sym == ">" || sym == ">=") return {4, false, false};
    if (sym == "NOT")                              return {3, true,  true};
    if (sym == "AND")                              return {2, false, false};
    if (sym == "OR")                               return {1, false, false};
    return {0, false, false};
}

// ===========================================================================
// Lexer
// ===========================================================================

inline std::vector<Token> QueryEngine::lex(const std::string& sql) const {
    std::vector<Token> out;
    const std::size_t n = sql.size();
    std::size_t i = 0;

    auto push = [&](Lexeme k, std::string s) { out.push_back(Token{k, std::move(s), 0}); };

    while (i < n) {
        const char c = sql[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

        // identifier / keyword / boolean operator word
        if (starts_word(c)) {
            std::size_t j = i;
            while (j < n && in_word(sql[j])) ++j;
            std::string raw = sql.substr(i, j - i);
            std::string up  = upcase(raw);
            i = j;

            if      (up == "SELECT") push(Lexeme::KwSelect, up);
            else if (up == "FROM")   push(Lexeme::KwFrom,   up);
            else if (up == "WHERE")  push(Lexeme::KwWhere,  up);
            else if (up == "ORDER")  push(Lexeme::KwOrder,  up);
            else if (up == "BY")     push(Lexeme::KwBy,     up);
            else if (up == "ASC")    push(Lexeme::KwAsc,    up);
            else if (up == "DESC")   push(Lexeme::KwDesc,   up);
            else if (up == "LIMIT")  push(Lexeme::KwLimit,  up);
            else if (up == "AND" || up == "OR" || up == "NOT")
                                     push(Lexeme::Operator, up);
            else                     push(Lexeme::Name,     raw);
            continue;
        }

        // integer literal
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t j = i;
            while (j < n && std::isdigit(static_cast<unsigned char>(sql[j]))) ++j;
            Token t{Lexeme::Number, sql.substr(i, j - i), 0};
            t.number = std::stoll(t.spelling);
            out.push_back(t);
            i = j;
            continue;
        }

        // single-quoted text; a doubled quote '' is one literal quote
        if (c == '\'') {
            std::string body;
            ++i;
            while (i < n) {
                if (sql[i] == '\'') {
                    if (i + 1 < n && sql[i + 1] == '\'') { body += '\''; i += 2; continue; }
                    ++i;
                    break;
                }
                body += sql[i++];
            }
            push(Lexeme::Text, body);
            continue;
        }

        // punctuation and comparison operators
        switch (c) {
            case '(': push(Lexeme::Open,  "("); ++i; break;
            case ')': push(Lexeme::Close, ")"); ++i; break;
            case ',': push(Lexeme::Comma, ","); ++i; break;
            case '*': push(Lexeme::Star,  "*"); ++i; break;
            case '=': push(Lexeme::Operator, "="); ++i; break;
            case '!':
                if (i + 1 < n && sql[i + 1] == '=') { push(Lexeme::Operator, "!="); i += 2; }
                else throw std::runtime_error("stray '!' (did you mean '!='?)");
                break;
            case '<':
                if (i + 1 < n && sql[i + 1] == '=') { push(Lexeme::Operator, "<="); i += 2; }
                else { push(Lexeme::Operator, "<"); ++i; }
                break;
            case '>':
                if (i + 1 < n && sql[i + 1] == '=') { push(Lexeme::Operator, ">="); i += 2; }
                else { push(Lexeme::Operator, ">"); ++i; }
                break;
            default:
                throw std::runtime_error(std::string("unexpected character: ") + c);
        }
    }

    push(Lexeme::Eof, "");
    return out;
}

// ===========================================================================
// Shunting-yard
// ===========================================================================

inline std::vector<Token> QueryEngine::to_postfix(const std::vector<Token>& infix) {
    std::vector<Token> out;
    std::vector<Token> ops;     // a std::vector used as the operator stack

    for (const Token& t : infix) {
        switch (t.kind) {
            case Lexeme::Name:
            case Lexeme::Number:
            case Lexeme::Text:
                out.push_back(t);
                break;

            case Lexeme::Open:
                ops.push_back(t);
                break;

            case Lexeme::Close:
                while (!ops.empty() && ops.back().kind != Lexeme::Open) {
                    out.push_back(ops.back());
                    ops.pop_back();
                }
                if (ops.empty()) throw std::runtime_error("unbalanced parentheses");
                ops.pop_back();   // drop the matching '('
                break;

            case Lexeme::Operator: {
                const OpRank cur = rank_of(t.spelling);
                while (!ops.empty() && ops.back().kind == Lexeme::Operator) {
                    const OpRank top = rank_of(ops.back().spelling);
                    const bool pop = top.power > cur.power ||
                                     (top.power == cur.power && !cur.right_assoc);
                    if (!pop) break;
                    out.push_back(ops.back());
                    ops.pop_back();
                }
                ops.push_back(t);
                break;
            }

            case Lexeme::Eof:
                break;          // sentinel — ignore

            default:
                throw std::runtime_error("token not allowed in WHERE: " + t.spelling);
        }
    }

    while (!ops.empty()) {
        if (ops.back().kind == Lexeme::Open) throw std::runtime_error("unbalanced parentheses");
        out.push_back(ops.back());
        ops.pop_back();
    }
    return out;
}

inline Cell QueryEngine::operand_of(const Token& t, const Relation& schema, const Tuple& row) {
    switch (t.kind) {
        case Lexeme::Number: return Cell{t.number};
        case Lexeme::Text:   return Cell{t.spelling};
        case Lexeme::Name: {
            const int idx = schema.column_of(t.spelling);
            if (idx < 0) throw std::runtime_error("unknown column: " + t.spelling);
            return row.fields[static_cast<std::size_t>(idx)];
        }
        default:
            throw std::runtime_error("expected an operand, found: " + t.spelling);
    }
}

inline bool QueryEngine::matches(const std::vector<Token>& postfix,
                                 const Relation& schema, const Tuple& row) {
    std::vector<Cell> st;

    for (const Token& t : postfix) {
        if (t.kind != Lexeme::Operator) {
            st.push_back(operand_of(t, schema, row));
            continue;
        }
        const OpRank info = rank_of(t.spelling);

        if (info.unary) {                                   // NOT
            if (st.empty()) throw std::runtime_error("NOT without an operand");
            const bool v = cell_true(st.back());
            st.back() = Cell{static_cast<long long>(!v)};
            continue;
        }

        if (st.size() < 2) throw std::runtime_error("operator '" + t.spelling + "' is missing operands");
        Cell rhs = st.back(); st.pop_back();
        Cell lhs = st.back(); st.pop_back();

        if (t.spelling == "AND") { st.push_back(Cell{static_cast<long long>(cell_true(lhs) && cell_true(rhs))}); continue; }
        if (t.spelling == "OR")  { st.push_back(Cell{static_cast<long long>(cell_true(lhs) || cell_true(rhs))}); continue; }

        bool comparable = true;
        const int c = cell_cmp(lhs, rhs, comparable);
        bool result = false;
        if (comparable) {
            if      (t.spelling == "=")  result = (c == 0);
            else if (t.spelling == "!=") result = (c != 0);
            else if (t.spelling == "<")  result = (c < 0);
            else if (t.spelling == "<=") result = (c <= 0);
            else if (t.spelling == ">")  result = (c > 0);
            else if (t.spelling == ">=") result = (c >= 0);
        }
        st.push_back(Cell{static_cast<long long>(result)});
    }

    if (st.empty()) return true;       // an empty predicate matches every row
    return cell_true(st.back());
}

inline std::string QueryEngine::postfix_text(const std::vector<Token>& postfix) {
    std::string s;
    for (const Token& t : postfix) {
        if (!s.empty()) s += ' ';
        if (t.kind == Lexeme::Text) s += "'" + t.spelling + "'";
        else                        s += t.spelling;
    }
    return s;
}

// ===========================================================================
// Compiler (recursive-descent over the token stream)
// ===========================================================================

inline Query QueryEngine::compile(const std::string& sql) const {
    const std::vector<Token> tk = lex(sql);
    Query q;
    std::size_t p = 0;

    auto want = [&](Lexeme k, const char* label) {
        if (tk[p].kind != k) throw std::runtime_error(std::string("expected ") + label);
    };

    want(Lexeme::KwSelect, "SELECT"); ++p;

    // projection list, or '*'
    if (tk[p].kind == Lexeme::Star) {
        ++p;                              // SELECT * -> empty projection list
    } else {
        for (;;) {
            want(Lexeme::Name, "a column name");
            q.select.push_back(tk[p].spelling);
            ++p;
            if (tk[p].kind == Lexeme::Comma) { ++p; continue; }
            break;
        }
    }

    want(Lexeme::KwFrom, "FROM"); ++p;
    want(Lexeme::Name, "a table name");
    q.from = tk[p].spelling; ++p;

    // optional WHERE: gather its tokens up to ORDER / LIMIT / Eof, then compile
    if (tk[p].kind == Lexeme::KwWhere) {
        ++p;
        std::vector<Token> infix;
        while (tk[p].kind != Lexeme::KwOrder &&
               tk[p].kind != Lexeme::KwLimit &&
               tk[p].kind != Lexeme::Eof) {
            infix.push_back(tk[p++]);
        }
        q.filter = to_postfix(infix);
    }

    // optional ORDER BY col [ASC|DESC]
    if (tk[p].kind == Lexeme::KwOrder) {
        ++p; want(Lexeme::KwBy, "BY"); ++p;
        want(Lexeme::Name, "a column after ORDER BY");
        q.sort_key = tk[p].spelling; ++p;
        if      (tk[p].kind == Lexeme::KwAsc)  ++p;
        else if (tk[p].kind == Lexeme::KwDesc) { q.sort_desc = true; ++p; }
    }

    // optional LIMIT n
    if (tk[p].kind == Lexeme::KwLimit) {
        ++p; want(Lexeme::Number, "an integer after LIMIT");
        q.cap = tk[p].number; ++p;
    }

    want(Lexeme::Eof, "end of query");
    return q;
}

// ===========================================================================
// Execution: filter -> project -> sort -> cap
// ===========================================================================

inline Relation QueryEngine::run(const Query& q, const Relation& src) const {
    if (q.from != src.name)
        throw std::runtime_error("no such table: " + q.from);

    // Resolve the output heading and the source column each output cell pulls from.
    const std::vector<std::string> out_cols = q.select.empty() ? src.heading : q.select;
    std::vector<int> pick;
    pick.reserve(out_cols.size());
    for (const std::string& c : out_cols) {
        const int idx = src.column_of(c);
        if (idx < 0) throw std::runtime_error("unknown column in projection: " + c);
        pick.push_back(idx);
    }

    Relation result;
    result.name    = src.name;
    result.heading = out_cols;

    // WHERE + projection in one pass.
    for (const Tuple& row : src.tuples) {
        if (!q.filter.empty() && !matches(q.filter, src, row)) continue;
        Tuple keep;
        keep.fields.reserve(pick.size());
        for (int idx : pick) keep.fields.push_back(row.fields[static_cast<std::size_t>(idx)]);
        result.tuples.push_back(std::move(keep));
    }

    // ORDER BY on the chosen output column.
    if (!q.sort_key.empty()) {
        const int oc = result.column_of(q.sort_key);
        if (oc < 0) throw std::runtime_error("ORDER BY column is not selected: " + q.sort_key);
        std::stable_sort(result.tuples.begin(), result.tuples.end(),
            [&](const Tuple& a, const Tuple& b) {
                bool comparable = true;
                const int c = cell_cmp(a.fields[static_cast<std::size_t>(oc)],
                                       b.fields[static_cast<std::size_t>(oc)], comparable);
                return q.sort_desc ? c > 0 : c < 0;
            });
    }

    // LIMIT.
    if (q.cap >= 0 && static_cast<long long>(result.tuples.size()) > q.cap)
        result.tuples.resize(static_cast<std::size_t>(q.cap));

    return result;
}

// ===========================================================================
// Table printer
// ===========================================================================

inline void QueryEngine::render(const Relation& r, std::ostream& os) {
    const std::size_t ncol = r.heading.size();
    std::vector<std::size_t> width(ncol);
    for (std::size_t c = 0; c < ncol; ++c) width[c] = r.heading[c].size();
    for (const Tuple& row : r.tuples)
        for (std::size_t c = 0; c < ncol; ++c)
            width[c] = std::max(width[c], cell_text(row.fields[c]).size());

    auto pad = [&](const std::string& s, std::size_t w) {
        os << s << std::string(w - s.size(), ' ');
    };
    auto sep = [&](std::size_t c) { os << (c + 1 < ncol ? " | " : "\n"); };

    for (std::size_t c = 0; c < ncol; ++c) { pad(r.heading[c], width[c]); sep(c); }
    for (std::size_t c = 0; c < ncol; ++c) { os << std::string(width[c], '-') << (c + 1 < ncol ? "-+-" : "\n"); }
    for (const Tuple& row : r.tuples)
        for (std::size_t c = 0; c < ncol; ++c) { pad(cell_text(row.fields[c]), width[c]); sep(c); }

    os << "(" << r.tuples.size() << " row" << (r.tuples.size() == 1 ? "" : "s") << ")\n";
}

}  // namespace kt

#endif  // KT_LAB7_MINI_SQL_HPP
