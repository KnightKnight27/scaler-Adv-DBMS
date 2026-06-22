// ============================================================================
// Lab 5 : Shunting-Yard expression evaluator + minimal SQL SELECT engine
// ----------------------------------------------------------------------------
// PART A : Dijkstra's Shunting-Yard algorithm
//          infix string  -->  tokens  -->  RPN (postfix)  -->  numeric result
//          operators: + - * / % ^  (^ right-associative), parentheses,
//          unary minus, integer & floating-point literals.
//
// PART B : A tiny SQL SELECT engine
//          SELECT <cols|*> FROM <table> WHERE <condition>
//          lexer -> tokens -> recursive-descent parser -> AST -> evaluation
//          over an in-memory std::vector<Row>.
//
// Build : g++ -std=c++17 *.cpp -o lab5
// ============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <stack>
#include <memory>
#include <stdexcept>
#include <cctype>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>

// ============================================================================
//  PART A  --  SHUNTING-YARD ARITHMETIC EVALUATOR
// ============================================================================

namespace arith {

// ---- Token model -----------------------------------------------------------
// Every piece of the input is reduced to one of these kinds. Keeping numbers
// and operators in a single token type lets the converter walk a flat list.
enum class TokKind { Number, Op, LParen, RParen };

struct Token {
    TokKind kind;
    double  value = 0.0;   // valid when kind == Number
    char    op    = 0;     // valid when kind == Op  (e.g. '+', 'u' = unary minus)
};

// ---- Operator metadata -----------------------------------------------------
// The whole correctness of Shunting-Yard hinges on a precedence + associativity
// table. Higher precedence binds tighter. Right-associative operators (like ^)
// are handled by a strict-vs-loose comparison when deciding to pop.
struct OpInfo {
    int  precedence;
    bool rightAssoc;
};

static OpInfo opInfo(char op) {
    switch (op) {
        case 'u': return {5, true};   // unary minus, binds very tightly, right-assoc
        case '^': return {4, true};   // exponentiation, right-associative
        case '*':
        case '/':
        case '%': return {3, false};
        case '+':
        case '-': return {2, false};
        default:  throw std::runtime_error("unknown operator");
    }
}

// ---- Lexer -----------------------------------------------------------------
// Scans the raw string and emits a vector<Token>. Unary minus is detected by
// context: a '-' is unary when it appears at the start, after another operator,
// or right after a '('. We mark it with the synthetic operator code 'u'.
static std::vector<Token> tokenize(const std::string& src) {
    std::vector<Token> out;
    size_t i = 0;
    auto prevAllowsUnary = [&]() -> bool {
        if (out.empty()) return true;                 // leading sign
        const Token& p = out.back();
        return p.kind == TokKind::Op || p.kind == TokKind::LParen;
    };

    while (i < src.size()) {
        char c = src[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

        // numeric literal: digits with an optional single decimal point
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            size_t j = i;
            bool seenDot = false;
            while (j < src.size() &&
                   (std::isdigit(static_cast<unsigned char>(src[j])) ||
                    (src[j] == '.' && !seenDot))) {
                if (src[j] == '.') seenDot = true;
                ++j;
            }
            Token t;
            t.kind  = TokKind::Number;
            t.value = std::stod(src.substr(i, j - i));
            out.push_back(t);
            i = j;
            continue;
        }

        if (c == '(') { out.push_back({TokKind::LParen}); ++i; continue; }
        if (c == ')') { out.push_back({TokKind::RParen}); ++i; continue; }

        // operators
        if (std::string("+-*/%^").find(c) != std::string::npos) {
            Token t;
            t.kind = TokKind::Op;
            t.op   = (c == '-' && prevAllowsUnary()) ? 'u' : c;
            out.push_back(t);
            ++i;
            continue;
        }
        throw std::runtime_error(std::string("unexpected character: ") + c);
    }
    return out;
}

// ---- Shunting-Yard : infix tokens -> RPN -----------------------------------
// Two storage areas: an output queue (the postfix result) and an operator
// stack. Numbers go straight to output. Operators are pushed only after every
// stacked operator of greater-or-equal binding (respecting associativity) has
// been flushed to output. Parentheses act as fences on the operator stack.
static std::vector<Token> toRPN(const std::vector<Token>& in) {
    std::vector<Token> output;
    std::stack<Token>  ops;

    for (const Token& t : in) {
        switch (t.kind) {
            case TokKind::Number:
                output.push_back(t);
                break;

            case TokKind::Op: {
                OpInfo cur = opInfo(t.op);
                while (!ops.empty() && ops.top().kind == TokKind::Op) {
                    OpInfo top = opInfo(ops.top().op);
                    // Pop while the stacked op binds tighter, OR binds equally
                    // and the current operator is left-associative. This single
                    // condition is what makes ^ right-associative and the rest
                    // left-associative.
                    bool pop = (top.precedence > cur.precedence) ||
                               (top.precedence == cur.precedence && !cur.rightAssoc);
                    if (!pop) break;
                    output.push_back(ops.top());
                    ops.pop();
                }
                ops.push(t);
                break;
            }

            case TokKind::LParen:
                ops.push(t);
                break;

            case TokKind::RParen:
                // flush back to the matching '('
                while (!ops.empty() && ops.top().kind != TokKind::LParen) {
                    output.push_back(ops.top());
                    ops.pop();
                }
                if (ops.empty())
                    throw std::runtime_error("mismatched parentheses");
                ops.pop();   // discard the '('
                break;
        }
    }
    // anything left on the stack is part of the trailing expression
    while (!ops.empty()) {
        if (ops.top().kind == TokKind::LParen)
            throw std::runtime_error("mismatched parentheses");
        output.push_back(ops.top());
        ops.pop();
    }
    return output;
}

// ---- RPN evaluation --------------------------------------------------------
// A postfix walk: push numbers, and on each operator pop its operands, apply,
// push the result. Because the order is already encoded, no precedence logic
// is needed here at all.
static double evalRPN(const std::vector<Token>& rpn) {
    std::stack<double> st;
    for (const Token& t : rpn) {
        if (t.kind == TokKind::Number) {
            st.push(t.value);
            continue;
        }
        if (t.op == 'u') {                         // unary minus: one operand
            if (st.empty()) throw std::runtime_error("missing operand for unary -");
            double a = st.top(); st.pop();
            st.push(-a);
            continue;
        }
        if (st.size() < 2) throw std::runtime_error("missing operand");
        double b = st.top(); st.pop();
        double a = st.top(); st.pop();
        switch (t.op) {
            case '+': st.push(a + b); break;
            case '-': st.push(a - b); break;
            case '*': st.push(a * b); break;
            case '/':
                if (b == 0.0) throw std::runtime_error("division by zero");
                st.push(a / b);
                break;
            case '%':
                if (b == 0.0) throw std::runtime_error("modulo by zero");
                st.push(std::fmod(a, b));
                break;
            case '^': st.push(std::pow(a, b)); break;
            default:  throw std::runtime_error("bad operator in RPN");
        }
    }
    if (st.size() != 1) throw std::runtime_error("malformed expression");
    return st.top();
}

// pretty-print an RPN token stream
static std::string rpnToString(const std::vector<Token>& rpn) {
    std::ostringstream os;
    for (size_t k = 0; k < rpn.size(); ++k) {
        if (k) os << ' ';
        const Token& t = rpn[k];
        if (t.kind == TokKind::Number) {
            // print integers without a trailing ".000000"
            if (t.value == std::floor(t.value) && std::abs(t.value) < 1e15)
                os << static_cast<long long>(t.value);
            else
                os << t.value;
        } else {
            os << (t.op == 'u' ? "(-)" : std::string(1, t.op));
        }
    }
    return os.str();
}

// convenience: full pipeline on one expression
static void runExpression(const std::string& expr) {
    auto toks   = tokenize(expr);
    auto rpn    = toRPN(toks);
    double res  = evalRPN(rpn);
    std::cout << "  infix : " << expr << "\n";
    std::cout << "  RPN   : " << rpnToString(rpn) << "\n";
    std::cout << "  result: " << res << "\n\n";
}

} // namespace arith


// ============================================================================
//  PART B  --  MINIMAL SQL SELECT ENGINE
// ============================================================================

namespace sql {

// ---- Value & Row model -----------------------------------------------------
// A cell is either a number or a string. We tag it so comparisons can decide
// how to behave. A Row is just an ordered list of (column -> value) pairs.
struct Value {
    enum class Type { Number, Str } type;
    double      num = 0.0;
    std::string str;

    static Value makeNum(double d) { Value v; v.type = Type::Number; v.num = d; return v; }
    static Value makeStr(std::string s) { Value v; v.type = Type::Str; v.str = std::move(s); return v; }
};

using Row   = std::map<std::string, Value>;
using Table = std::vector<Row>;

// ---- Lexer -----------------------------------------------------------------
// Token kinds the SQL grammar cares about. Keywords are recognised after the
// fact (case-insensitive) so the lexer itself only needs identifier scanning.
enum class Tk {
    Select, From, Where, And, Or,
    Star, Comma, LParen, RParen,
    Eq, Ne, Lt, Le, Gt, Ge,
    Ident, Number, String,
    End
};

struct SqlToken {
    Tk          kind;
    std::string text;   // identifier / string contents
    double      num = 0; // numeric literal
};

static std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

static std::vector<SqlToken> lex(const std::string& src) {
    std::vector<SqlToken> out;
    size_t i = 0;
    while (i < src.size()) {
        char c = src[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

        // single-quoted string literal
        if (c == '\'') {
            ++i;
            std::string s;
            while (i < src.size() && src[i] != '\'') s += src[i++];
            if (i >= src.size()) throw std::runtime_error("unterminated string literal");
            ++i; // closing quote
            out.push_back({Tk::String, s});
            continue;
        }

        // numeric literal
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            size_t j = i; bool dot = false;
            while (j < src.size() &&
                   (std::isdigit(static_cast<unsigned char>(src[j])) ||
                    (src[j] == '.' && !dot))) {
                if (src[j] == '.') dot = true;
                ++j;
            }
            SqlToken t{Tk::Number, src.substr(i, j - i)};
            t.num = std::stod(t.text);
            out.push_back(t);
            i = j;
            continue;
        }

        // identifier or keyword
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t j = i;
            while (j < src.size() &&
                   (std::isalnum(static_cast<unsigned char>(src[j])) || src[j] == '_'))
                ++j;
            std::string word = src.substr(i, j - i);
            std::string up   = toUpper(word);
            i = j;
            if      (up == "SELECT") out.push_back({Tk::Select, word});
            else if (up == "FROM")   out.push_back({Tk::From,   word});
            else if (up == "WHERE")  out.push_back({Tk::Where,  word});
            else if (up == "AND")    out.push_back({Tk::And,    word});
            else if (up == "OR")     out.push_back({Tk::Or,     word});
            else                     out.push_back({Tk::Ident,  word});
            continue;
        }

        // operators & punctuation (two-char operators checked first)
        auto two = src.substr(i, 2);
        if      (two == "!=") { out.push_back({Tk::Ne, "!="}); i += 2; continue; }
        if      (two == "<=") { out.push_back({Tk::Le, "<="}); i += 2; continue; }
        if      (two == ">=") { out.push_back({Tk::Ge, ">="}); i += 2; continue; }

        switch (c) {
            case '*': out.push_back({Tk::Star,   "*"}); break;
            case ',': out.push_back({Tk::Comma,  ","}); break;
            case '(': out.push_back({Tk::LParen, "("}); break;
            case ')': out.push_back({Tk::RParen, ")"}); break;
            case '=': out.push_back({Tk::Eq,     "="}); break;
            case '<': out.push_back({Tk::Lt,     "<"}); break;
            case '>': out.push_back({Tk::Gt,     ">"}); break;
            default:
                throw std::runtime_error(std::string("unexpected character: ") + c);
        }
        ++i;
    }
    out.push_back({Tk::End, ""});
    return out;
}

// ---- AST for the WHERE predicate -------------------------------------------
// Three node shapes: a column reference, a literal, and a binary node that
// covers both comparisons (= < > ...) and the logical connectives (AND/OR).
struct Expr {
    enum class Kind { Column, Literal, Binary } kind;

    // Column
    std::string column;
    // Literal
    Value lit;
    // Binary
    Tk    op = Tk::End;
    std::unique_ptr<Expr> lhs, rhs;
};

using ExprPtr = std::unique_ptr<Expr>;

// ---- Parsed query ----------------------------------------------------------
struct Query {
    bool                     selectAll = false;
    std::vector<std::string> columns;
    std::string              table;
    ExprPtr                  where;   // may be null (no WHERE clause)
};

// ---- Recursive-descent parser ----------------------------------------------
// Grammar (see README):
//   query      := SELECT select_list FROM ident [ WHERE or_expr ]
//   or_expr    := and_expr (OR and_expr)*
//   and_expr   := comparison (AND comparison)*
//   comparison := primary (cmp_op primary)?
//   primary    := '(' or_expr ')' | ident | number | string
// The precedence climb is encoded by the call chain: OR sits above AND, which
// sits above comparison, so AND binds tighter than OR automatically.
class Parser {
public:
    explicit Parser(std::vector<SqlToken> toks) : t_(std::move(toks)) {}

    Query parse() {
        Query q;
        expect(Tk::Select);
        parseSelectList(q);
        expect(Tk::From);
        q.table = expectIdent();
        if (peek().kind == Tk::Where) {
            advance();
            q.where = parseOr();
        }
        expect(Tk::End);
        return q;
    }

private:
    std::vector<SqlToken> t_;
    size_t pos_ = 0;

    const SqlToken& peek() const { return t_[pos_]; }
    const SqlToken& advance()    { return t_[pos_++]; }

    void expect(Tk k) {
        if (peek().kind != k)
            throw std::runtime_error("syntax error near '" + peek().text + "'");
        advance();
    }
    std::string expectIdent() {
        if (peek().kind != Tk::Ident)
            throw std::runtime_error("expected identifier near '" + peek().text + "'");
        return advance().text;
    }

    void parseSelectList(Query& q) {
        if (peek().kind == Tk::Star) {
            advance();
            q.selectAll = true;
            return;
        }
        q.columns.push_back(expectIdent());
        while (peek().kind == Tk::Comma) {
            advance();
            q.columns.push_back(expectIdent());
        }
    }

    ExprPtr parseOr() {
        ExprPtr lhs = parseAnd();
        while (peek().kind == Tk::Or) {
            advance();
            ExprPtr rhs = parseAnd();
            lhs = makeBinary(Tk::Or, std::move(lhs), std::move(rhs));
        }
        return lhs;
    }

    ExprPtr parseAnd() {
        ExprPtr lhs = parseComparison();
        while (peek().kind == Tk::And) {
            advance();
            ExprPtr rhs = parseComparison();
            lhs = makeBinary(Tk::And, std::move(lhs), std::move(rhs));
        }
        return lhs;
    }

    static bool isCmp(Tk k) {
        return k == Tk::Eq || k == Tk::Ne || k == Tk::Lt ||
               k == Tk::Le || k == Tk::Gt || k == Tk::Ge;
    }

    ExprPtr parseComparison() {
        ExprPtr lhs = parsePrimary();
        if (isCmp(peek().kind)) {
            Tk op = advance().kind;
            ExprPtr rhs = parsePrimary();
            return makeBinary(op, std::move(lhs), std::move(rhs));
        }
        return lhs;
    }

    ExprPtr parsePrimary() {
        const SqlToken& tk = peek();
        if (tk.kind == Tk::LParen) {
            advance();
            ExprPtr e = parseOr();
            expect(Tk::RParen);
            return e;
        }
        if (tk.kind == Tk::Ident) {
            advance();
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::Column;
            e->column = tk.text;
            return e;
        }
        if (tk.kind == Tk::Number) {
            advance();
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::Literal;
            e->lit  = Value::makeNum(tk.num);
            return e;
        }
        if (tk.kind == Tk::String) {
            advance();
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Kind::Literal;
            e->lit  = Value::makeStr(tk.text);
            return e;
        }
        throw std::runtime_error("expected value near '" + tk.text + "'");
    }

    static ExprPtr makeBinary(Tk op, ExprPtr l, ExprPtr r) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Kind::Binary;
        e->op   = op;
        e->lhs  = std::move(l);
        e->rhs  = std::move(r);
        return e;
    }
};

// ---- Evaluation over a single row ------------------------------------------
// Resolve a leaf to a concrete Value, then apply comparison / logic. Logical
// connectives evaluate their children to a bool (a comparison result), so the
// AST is walked once per row.

static Value resolve(const Expr* e, const Row& row) {
    switch (e->kind) {
        case Expr::Kind::Column: {
            auto it = row.find(e->column);
            if (it == row.end())
                throw std::runtime_error("unknown column: " + e->column);
            return it->second;
        }
        case Expr::Kind::Literal:
            return e->lit;
        default:
            throw std::runtime_error("expected a leaf value");
    }
}

static int compareValues(const Value& a, const Value& b) {
    // numeric comparison when both numeric, lexical otherwise
    if (a.type == Value::Type::Number && b.type == Value::Type::Number) {
        if (a.num < b.num) return -1;
        if (a.num > b.num) return  1;
        return 0;
    }
    const std::string& sa = (a.type == Value::Type::Str) ? a.str : std::to_string(a.num);
    const std::string& sb = (b.type == Value::Type::Str) ? b.str : std::to_string(b.num);
    return sa.compare(sb) < 0 ? -1 : (sa.compare(sb) > 0 ? 1 : 0);
}

static bool evalPredicate(const Expr* e, const Row& row) {
    if (e->kind == Expr::Kind::Binary) {
        if (e->op == Tk::And) return evalPredicate(e->lhs.get(), row) &&
                                     evalPredicate(e->rhs.get(), row);
        if (e->op == Tk::Or)  return evalPredicate(e->lhs.get(), row) ||
                                     evalPredicate(e->rhs.get(), row);
        // comparison
        Value l = resolve(e->lhs.get(), row);
        Value r = resolve(e->rhs.get(), row);
        int c = compareValues(l, r);
        switch (e->op) {
            case Tk::Eq: return c == 0;
            case Tk::Ne: return c != 0;
            case Tk::Lt: return c <  0;
            case Tk::Le: return c <= 0;
            case Tk::Gt: return c >  0;
            case Tk::Ge: return c >= 0;
            default: throw std::runtime_error("bad comparison operator");
        }
    }
    throw std::runtime_error("WHERE clause must be a boolean expression");
}

// ---- Printing helpers ------------------------------------------------------
static std::string valueToString(const Value& v) {
    if (v.type == Value::Type::Str) return v.str;
    std::ostringstream os;
    if (v.num == std::floor(v.num) && std::abs(v.num) < 1e15)
        os << static_cast<long long>(v.num);
    else
        os << v.num;
    return os.str();
}

static void printRow(const Query& q, const Row& row) {
    std::vector<std::string> cols;
    if (q.selectAll)
        for (const auto& kv : row) cols.push_back(kv.first);
    else
        cols = q.columns;

    std::cout << "    ";
    for (size_t i = 0; i < cols.size(); ++i) {
        if (i) std::cout << " | ";
        auto it = row.find(cols[i]);
        std::cout << cols[i] << "="
                  << (it == row.end() ? "<null>" : valueToString(it->second));
    }
    std::cout << "\n";
}

// ---- Driver : parse + execute one query ------------------------------------
static void runQuery(const std::string& text,
                     const std::map<std::string, Table>& db) {
    std::cout << "  query : " << text << "\n";
    Query q = Parser(lex(text)).parse();

    auto it = db.find(q.table);
    if (it == db.end()) throw std::runtime_error("no such table: " + q.table);
    const Table& tbl = it->second;

    int matched = 0;
    for (const Row& row : tbl) {
        if (!q.where || evalPredicate(q.where.get(), row)) {
            printRow(q, row);
            ++matched;
        }
    }
    std::cout << "  (" << matched << " row(s) matched)\n\n";
}

} // namespace sql


// ============================================================================
//  MAIN  --  exercises both parts
// ============================================================================

static sql::Row makeStudent(int id, const std::string& name, int age, double gpa) {
    using sql::Value;
    sql::Row r;
    r["id"]   = Value::makeNum(id);
    r["name"] = Value::makeStr(name);
    r["age"]  = Value::makeNum(age);
    r["gpa"]  = Value::makeNum(gpa);
    return r;
}

int main() {
    std::cout << std::fixed << std::setprecision(4);

    // ---------------- PART A : expressions ----------------
    std::cout << "=== PART A : Shunting-Yard expression evaluator ===\n\n";
    const char* expressions[] = {
        "3 + 4 * 2",
        "(3 + 4) * 2",
        "2 ^ 3 ^ 2",          // right-assoc: 2^(3^2) = 512
        "-5 + 3 * -2",        // unary minus
        "10 % 3 + 1.5",
        "((1 + 2) * (3 + 4)) / 7"
    };
    for (const char* e : expressions) {
        try { arith::runExpression(e); }
        catch (const std::exception& ex) {
            std::cout << "  error in '" << e << "': " << ex.what() << "\n\n";
        }
    }

    // ---------------- PART B : SQL SELECT ----------------
    std::cout << "=== PART B : minimal SQL SELECT engine ===\n\n";

    std::map<std::string, sql::Table> db;
    db["students"] = {
        makeStudent(1, "Alice",   20, 3.9),
        makeStudent(2, "Bob",     22, 2.8),
        makeStudent(3, "Charlie", 19, 3.5),
        makeStudent(4, "Diana",   23, 3.2),
        makeStudent(5, "Eve",     21, 2.4),
    };

    const char* queries[] = {
        "SELECT * FROM students",
        "SELECT name, gpa FROM students WHERE gpa >= 3.5",
        "SELECT id, name FROM students WHERE age > 20 AND gpa < 3.0",
        "SELECT name FROM students WHERE name = 'Bob' OR age <= 19",
        "SELECT * FROM students WHERE (gpa > 3.0 AND age < 21) OR id = 5",
    };
    for (const char* qstr : queries) {
        try { sql::runQuery(qstr, db); }
        catch (const std::exception& ex) {
            std::cout << "  error in query: " << ex.what() << "\n\n";
        }
    }

    return 0;
}
