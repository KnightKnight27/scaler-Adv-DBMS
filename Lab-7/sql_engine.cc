// sql_engine.cc — ADBMS Lab 7, 24BCS10115 Gauri Shukla
//
// Implementation of the minimal SQL SELECT engine declared in sql_engine.h.

#include "sql_engine.h"

#include <algorithm>
#include <cctype>
#include <ostream>
#include <stack>
#include <stdexcept>
#include <string>

namespace sqlmini {

namespace {

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool is_word_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool is_word_char(char c)  { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// Operator metadata for the shunting-yard. Larger rank binds tighter.
struct OpInfo { int rank; bool unary; bool right_assoc; };

OpInfo op_info(const std::string& sym) {
    if (sym == "=" || sym == "!=" || sym == "<" ||
        sym == "<=" || sym == ">" || sym == ">=") return {4, false, false};
    if (sym == "NOT")                              return {3, true,  true};
    if (sym == "AND")                              return {2, false, false};
    if (sym == "OR")                               return {1, false, false};
    return {0, false, false};   // not an operator
}

// Truthiness of a value in a boolean context.
bool truthy(const Value& v) {
    if (std::holds_alternative<long long>(v)) return std::get<long long>(v) != 0;
    return !std::get<std::string>(v).empty();
}

// Three-way compare of two values. Ints compare numerically, strings
// lexicographically; a numeric/text mismatch is reported as unorderable.
int compare(const Value& a, const Value& b, bool& ok) {
    ok = true;
    if (std::holds_alternative<long long>(a) && std::holds_alternative<long long>(b)) {
        long long x = std::get<long long>(a), y = std::get<long long>(b);
        return (x < y) ? -1 : (x > y) ? 1 : 0;
    }
    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
        const std::string& x = std::get<std::string>(a);
        const std::string& y = std::get<std::string>(b);
        return (x < y) ? -1 : (x > y) ? 1 : 0;
    }
    ok = false;
    return 0;
}

// Resolve an operand token to a concrete Value (column lookup for identifiers).
Value operand_value(const Token& t, const Table& schema, const Row& r) {
    switch (t.kind) {
        case Tok::Int: return Value{t.ival};
        case Tok::Str: return Value{t.text};
        case Tok::Ident: {
            int idx = schema.col_index(t.text);
            if (idx < 0) throw std::runtime_error("unknown column: " + t.text);
            return r.cells[static_cast<std::size_t>(idx)];
        }
        default: throw std::runtime_error("not an operand: " + t.text);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Table helper
// ---------------------------------------------------------------------------

int Table::col_index(const std::string& c) const {
    for (std::size_t i = 0; i < columns.size(); ++i)
        if (columns[i] == c) return static_cast<int>(i);
    return -1;
}

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

std::vector<Token> tokenize(const std::string& sql) {
    std::vector<Token> out;
    const int n = static_cast<int>(sql.size());
    int i = 0;

    while (i < n) {
        char c = sql[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

        // word: keyword, boolean operator, or identifier
        if (is_word_start(c)) {
            int j = i;
            while (j < n && is_word_char(sql[j])) ++j;
            std::string w = sql.substr(static_cast<std::size_t>(i), static_cast<std::size_t>(j - i));
            std::string W = upper(w);
            i = j;
            if      (W == "SELECT") out.push_back({Tok::Select, W});
            else if (W == "FROM")   out.push_back({Tok::From,   W});
            else if (W == "WHERE")  out.push_back({Tok::Where,  W});
            else if (W == "ORDER")  out.push_back({Tok::Order,  W});
            else if (W == "BY")     out.push_back({Tok::By,     W});
            else if (W == "ASC")    out.push_back({Tok::Asc,    W});
            else if (W == "DESC")   out.push_back({Tok::Desc,   W});
            else if (W == "LIMIT")  out.push_back({Tok::Limit,  W});
            else if (W == "AND" || W == "OR" || W == "NOT")
                                    out.push_back({Tok::Op,     W});
            else                    out.push_back({Tok::Ident,  w});
            continue;
        }

        // integer literal
        if (std::isdigit(static_cast<unsigned char>(c))) {
            int j = i;
            while (j < n && std::isdigit(static_cast<unsigned char>(sql[j]))) ++j;
            Token t{Tok::Int, sql.substr(static_cast<std::size_t>(i), static_cast<std::size_t>(j - i))};
            t.ival = std::stoll(t.text);
            out.push_back(t);
            i = j;
            continue;
        }

        // single-quoted string literal ('' escapes a quote)
        if (c == '\'') {
            std::string s;
            ++i;
            while (i < n) {
                if (sql[i] == '\'') {
                    if (i + 1 < n && sql[i + 1] == '\'') { s += '\''; i += 2; continue; }
                    ++i; break;
                }
                s += sql[i++];
            }
            out.push_back({Tok::Str, s});
            continue;
        }

        // punctuation + operators
        switch (c) {
            case '(': out.push_back({Tok::LParen, "("}); ++i; break;
            case ')': out.push_back({Tok::RParen, ")"}); ++i; break;
            case ',': out.push_back({Tok::Comma,  ","}); ++i; break;
            case '*': out.push_back({Tok::Star,   "*"}); ++i; break;
            case '=': out.push_back({Tok::Op,     "="}); ++i; break;
            case '!':
                if (i + 1 < n && sql[i + 1] == '=') { out.push_back({Tok::Op, "!="}); i += 2; }
                else throw std::runtime_error("unexpected '!'");
                break;
            case '<':
                if (i + 1 < n && sql[i + 1] == '=') { out.push_back({Tok::Op, "<="}); i += 2; }
                else { out.push_back({Tok::Op, "<"}); ++i; }
                break;
            case '>':
                if (i + 1 < n && sql[i + 1] == '=') { out.push_back({Tok::Op, ">="}); i += 2; }
                else { out.push_back({Tok::Op, ">"}); ++i; }
                break;
            default:
                throw std::runtime_error(std::string("unexpected character: ") + c);
        }
    }

    out.push_back({Tok::End, ""});
    return out;
}

// ---------------------------------------------------------------------------
// Shunting-yard + RPN evaluation
// ---------------------------------------------------------------------------

std::vector<Token> shunting_yard(const std::vector<Token>& infix) {
    std::vector<Token> output;
    std::stack<Token>  ops;

    for (const Token& t : infix) {
        switch (t.kind) {
            case Tok::Ident:
            case Tok::Int:
            case Tok::Str:
                output.push_back(t);
                break;
            case Tok::LParen:
                ops.push(t);
                break;
            case Tok::RParen:
                while (!ops.empty() && ops.top().kind != Tok::LParen) {
                    output.push_back(ops.top());
                    ops.pop();
                }
                if (ops.empty()) throw std::runtime_error("mismatched parentheses");
                ops.pop();   // discard the '('
                break;
            case Tok::Op: {
                OpInfo o1 = op_info(t.text);
                while (!ops.empty() && ops.top().kind == Tok::Op) {
                    OpInfo o2 = op_info(ops.top().text);
                    bool pop = (o2.rank > o1.rank) ||
                               (o2.rank == o1.rank && !o1.right_assoc);
                    if (!pop) break;
                    output.push_back(ops.top());
                    ops.pop();
                }
                ops.push(t);
                break;
            }
            case Tok::End:
                break;     // sentinel — ignore
            default:
                throw std::runtime_error("token not valid in WHERE: " + t.text);
        }
    }
    while (!ops.empty()) {
        if (ops.top().kind == Tok::LParen) throw std::runtime_error("mismatched parentheses");
        output.push_back(ops.top());
        ops.pop();
    }
    return output;
}

bool eval_rpn(const std::vector<Token>& rpn, const Table& schema, const Row& r) {
    std::stack<Value> st;

    for (const Token& t : rpn) {
        if (t.kind != Tok::Op) {
            st.push(operand_value(t, schema, r));
            continue;
        }
        OpInfo info = op_info(t.text);

        if (info.unary) {                       // NOT
            if (st.empty()) throw std::runtime_error("NOT missing operand");
            bool v = truthy(st.top()); st.pop();
            st.push(Value{static_cast<long long>(!v)});
            continue;
        }

        if (st.size() < 2) throw std::runtime_error("operator missing operands: " + t.text);
        Value rhs = st.top(); st.pop();
        Value lhs = st.top(); st.pop();

        if (t.text == "AND") { st.push(Value{static_cast<long long>(truthy(lhs) && truthy(rhs))}); continue; }
        if (t.text == "OR")  { st.push(Value{static_cast<long long>(truthy(lhs) || truthy(rhs))}); continue; }

        bool ok = true;
        int cmp = compare(lhs, rhs, ok);
        bool res = false;
        if (ok) {
            if      (t.text == "=")  res = (cmp == 0);
            else if (t.text == "!=") res = (cmp != 0);
            else if (t.text == "<")  res = (cmp < 0);
            else if (t.text == "<=") res = (cmp <= 0);
            else if (t.text == ">")  res = (cmp > 0);
            else if (t.text == ">=") res = (cmp >= 0);
        }
        st.push(Value{static_cast<long long>(res)});
    }

    if (st.empty()) return true;       // empty predicate matches everything
    return truthy(st.top());
}

std::string rpn_to_string(const std::vector<Token>& rpn) {
    std::string s;
    for (const Token& t : rpn) {
        if (!s.empty()) s += ' ';
        if (t.kind == Tok::Str) s += "'" + t.text + "'";
        else                    s += t.text;
    }
    return s;
}

// ---------------------------------------------------------------------------
// SELECT parser
// ---------------------------------------------------------------------------

SelectStmt parse_select(const std::string& sql) {
    std::vector<Token> tk = tokenize(sql);
    SelectStmt q;
    std::size_t p = 0;

    auto expect = [&](Tok k, const char* what) {
        if (tk[p].kind != k) throw std::runtime_error(std::string("expected ") + what);
    };

    expect(Tok::Select, "SELECT"); ++p;

    // projection list
    if (tk[p].kind == Tok::Star) {
        ++p;                                   // SELECT * -> empty projection
    } else {
        while (true) {
            expect(Tok::Ident, "column name");
            q.projection.push_back(tk[p].text);
            ++p;
            if (tk[p].kind == Tok::Comma) { ++p; continue; }
            break;
        }
    }

    expect(Tok::From, "FROM"); ++p;
    expect(Tok::Ident, "table name");
    q.table = tk[p].text; ++p;

    // optional WHERE — collect its tokens until ORDER / LIMIT / End
    if (tk[p].kind == Tok::Where) {
        ++p;
        std::vector<Token> infix;
        while (tk[p].kind != Tok::Order && tk[p].kind != Tok::Limit && tk[p].kind != Tok::End)
            infix.push_back(tk[p++]);
        q.where_rpn = shunting_yard(infix);
    }

    // optional ORDER BY col [ASC|DESC]
    if (tk[p].kind == Tok::Order) {
        ++p; expect(Tok::By, "BY"); ++p;
        expect(Tok::Ident, "column name after ORDER BY");
        q.order_by = tk[p].text; ++p;
        if (tk[p].kind == Tok::Asc)  ++p;
        else if (tk[p].kind == Tok::Desc) { q.order_desc = true; ++p; }
    }

    // optional LIMIT n
    if (tk[p].kind == Tok::Limit) {
        ++p; expect(Tok::Int, "integer after LIMIT");
        q.limit = tk[p].ival; ++p;
    }

    expect(Tok::End, "end of query");
    return q;
}

// ---------------------------------------------------------------------------
// Executor: filter -> project -> order -> limit
// ---------------------------------------------------------------------------

Table execute(const SelectStmt& q, const Table& src) {
    if (q.table != src.name)
        throw std::runtime_error("no such table: " + q.table);

    // Decide the output columns (and their source indices).
    std::vector<std::string> out_cols =
        q.projection.empty() ? src.columns : q.projection;
    std::vector<int> src_idx;
    src_idx.reserve(out_cols.size());
    for (const std::string& c : out_cols) {
        int idx = src.col_index(c);
        if (idx < 0) throw std::runtime_error("unknown column in projection: " + c);
        src_idx.push_back(idx);
    }

    Table result;
    result.name    = src.name;
    result.columns = out_cols;

    // filter (WHERE) + project
    for (const Row& r : src.rows) {
        if (!q.where_rpn.empty() && !eval_rpn(q.where_rpn, src, r)) continue;
        Row projected;
        projected.cells.reserve(src_idx.size());
        for (int idx : src_idx) projected.cells.push_back(r.cells[static_cast<std::size_t>(idx)]);
        result.rows.push_back(std::move(projected));
    }

    // ORDER BY (sort by the column's position in the *result*)
    if (!q.order_by.empty()) {
        int oc = result.col_index(q.order_by);
        if (oc < 0) throw std::runtime_error("ORDER BY column not in projection: " + q.order_by);
        std::stable_sort(result.rows.begin(), result.rows.end(),
            [&](const Row& a, const Row& b) {
                bool ok = true;
                int cmp = compare(a.cells[static_cast<std::size_t>(oc)],
                                  b.cells[static_cast<std::size_t>(oc)], ok);
                return q.order_desc ? cmp > 0 : cmp < 0;
            });
    }

    // LIMIT
    if (q.limit >= 0 && static_cast<long long>(result.rows.size()) > q.limit)
        result.rows.resize(static_cast<std::size_t>(q.limit));

    return result;
}

// ---------------------------------------------------------------------------
// Pretty printer
// ---------------------------------------------------------------------------

void print_table(const Table& t, std::ostream& os) {
    const std::size_t ncol = t.columns.size();
    std::vector<std::size_t> width(ncol);
    for (std::size_t c = 0; c < ncol; ++c) width[c] = t.columns[c].size();

    auto cell_str = [](const Value& v) -> std::string {
        if (std::holds_alternative<long long>(v)) return std::to_string(std::get<long long>(v));
        return std::get<std::string>(v);
    };

    for (const Row& r : t.rows)
        for (std::size_t c = 0; c < ncol; ++c)
            width[c] = std::max(width[c], cell_str(r.cells[c]).size());

    auto pad = [&](const std::string& s, std::size_t w) {
        os << s << std::string(w - s.size(), ' ');
    };

    for (std::size_t c = 0; c < ncol; ++c) { pad(t.columns[c], width[c]); os << (c + 1 < ncol ? " | " : "\n"); }
    for (std::size_t c = 0; c < ncol; ++c) { os << std::string(width[c], '-') << (c + 1 < ncol ? "-+-" : "\n"); }
    for (const Row& r : t.rows) {
        for (std::size_t c = 0; c < ncol; ++c) { pad(cell_str(r.cells[c]), width[c]); os << (c + 1 < ncol ? " | " : "\n"); }
    }
    os << "(" << t.rows.size() << " row" << (t.rows.size() == 1 ? "" : "s") << ")\n";
}

}  // namespace sqlmini
