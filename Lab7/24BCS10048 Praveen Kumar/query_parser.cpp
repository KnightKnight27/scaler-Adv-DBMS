/*
 * =============================================================================
 *  Lab 7 -- Part A: SQL Query Parser (Lexer + Recursive-Descent)
 * =============================================================================
 *
 *  Course  : Advanced DBMS (Scaler)
 *  Author  : Praveen Kumar
 *  Date    : 2026-06-22
 *
 *  Purpose : Show how a database front-end turns a raw SQL string into
 *            structured operations.  The program:
 *              1. Lexes a SQL SELECT ... WHERE ... string into tokens.
 *              2. Parses the WHERE clause into an AST using recursive descent.
 *              3. Evaluates the AST against an in-memory table and prints
 *                 matching rows.
 *
 *  This is a toy implementation.  Real parsers (PostgreSQL gram.y, MySQL's
 *  sql/sql_yacc.yy) are much larger but apply exactly the same stages.
 *
 *  Grammar (simplified):
 *      query     -> SELECT * FROM table_name WHERE expr
 *      expr      -> term (OR term)*
 *      term      -> factor (AND factor)*
 *      factor    -> NOT factor | LPAREN expr RPAREN | comparison
 *      comparison-> column op value
 *      op        -> = | < | > | <= | >=
 *
 *  Build  : g++ -std=c++17 -O2 -Wall -Wextra -o query_parser query_parser.cpp
 *  Run    : ./query_parser
 * =============================================================================
 */

#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <memory>
#include <cctype>
#include <sstream>

/* ===========================================================================
 *  In-memory table
 * =========================================================================== */

struct Employee {
    int         id;
    std::string name;
    int         age;
    std::string dept;
};

static const std::vector<Employee> EMPLOYEES = {
    {1,  "Alice",   28, "Engineering"},
    {2,  "Bob",     35, "Marketing"},
    {3,  "Carol",   22, "Engineering"},
    {4,  "Dave",    40, "HR"},
    {5,  "Eve",     31, "Engineering"},
    {6,  "Frank",   26, "Marketing"},
    {7,  "Grace",   45, "HR"},
    {8,  "Heidi",   29, "Engineering"},
    {9,  "Ivan",    33, "Marketing"},
    {10, "Judy",    38, "HR"},
};

/* ===========================================================================
 *  Lexer
 * =========================================================================== */

enum class TK {
    KW_SELECT, KW_FROM, KW_WHERE, KW_AND, KW_OR, KW_NOT,
    STAR,
    NAME,           /* identifier or unquoted string value */
    INT_LIT,        /* integer literal */
    STR_LIT,        /* 'quoted string' */
    EQ,             /* =  */
    LT,             /* <  */
    GT,             /* >  */
    LE,             /* <= */
    GE,             /* >= */
    NE,             /* != */
    LPAREN, RPAREN,
    END
};

struct Token {
    TK          kind;
    std::string text;
};

static std::vector<Token> tokenize(const std::string &sql)
{
    std::vector<Token> out;
    size_t i = 0;

    auto peek = [&]() -> char { return i < sql.size() ? sql[i] : '\0'; };
    auto next = [&]() -> char { return sql[i++]; };

    while (i < sql.size()) {
        /* skip whitespace */
        if (std::isspace(peek())) { next(); continue; }

        /* quoted string literal */
        if (peek() == '\'') {
            next();
            std::string s;
            while (i < sql.size() && peek() != '\'') s += next();
            if (i < sql.size()) next(); /* closing ' */
            out.push_back({TK::STR_LIT, s});
            continue;
        }

        /* identifier or keyword */
        if (std::isalpha(peek()) || peek() == '_') {
            std::string word;
            while (i < sql.size() && (std::isalnum(peek()) || peek() == '_'))
                word += next();
            /* uppercase for keyword matching */
            std::string up = word;
            std::transform(up.begin(), up.end(), up.begin(), ::toupper);

            if      (up == "SELECT") out.push_back({TK::KW_SELECT, word});
            else if (up == "FROM")   out.push_back({TK::KW_FROM,   word});
            else if (up == "WHERE")  out.push_back({TK::KW_WHERE,  word});
            else if (up == "AND")    out.push_back({TK::KW_AND,    word});
            else if (up == "OR")     out.push_back({TK::KW_OR,     word});
            else if (up == "NOT")    out.push_back({TK::KW_NOT,    word});
            else                     out.push_back({TK::NAME,       word});
            continue;
        }

        /* integer literal */
        if (std::isdigit(peek())) {
            std::string num;
            while (i < sql.size() && std::isdigit(peek())) num += next();
            out.push_back({TK::INT_LIT, num});
            continue;
        }

        /* operators and punctuation */
        char c = next();
        switch (c) {
        case '*': out.push_back({TK::STAR,   "*"}); break;
        case '(': out.push_back({TK::LPAREN, "("}); break;
        case ')': out.push_back({TK::RPAREN, ")"}); break;
        case '=': out.push_back({TK::EQ,     "="}); break;
        case '<':
            if (peek() == '=') { next(); out.push_back({TK::LE, "<="}); }
            else                          out.push_back({TK::LT, "<"});
            break;
        case '>':
            if (peek() == '=') { next(); out.push_back({TK::GE, ">="}); }
            else                          out.push_back({TK::GT, ">"});
            break;
        case '!':
            if (peek() == '=') { next(); out.push_back({TK::NE, "!="}); }
            break;
        default:
            /* ignore unknown characters */
            break;
        }
    }

    out.push_back({TK::END, ""});
    return out;
}

/* ===========================================================================
 *  AST nodes
 * =========================================================================== */

struct Expr {
    virtual ~Expr() = default;
    virtual bool eval(const Employee &row) const = 0;
    virtual std::string str() const = 0;
};

using ExprPtr = std::unique_ptr<Expr>;

/* Leaf: column OP value */
struct Cmp : Expr {
    std::string col;
    TK          op;
    std::string val;

    Cmp(std::string c, TK o, std::string v)
        : col(std::move(c)), op(o), val(std::move(v)) {}

    /* Resolve column to integer */
    int intVal(const Employee &row) const {
        if (col == "id")  return row.id;
        if (col == "age") return row.age;
        throw std::runtime_error("Column not an integer: " + col);
    }

    /* Resolve column to string */
    std::string strVal(const Employee &row) const {
        if (col == "name") return row.name;
        if (col == "dept") return row.dept;
        /* int columns as string */
        if (col == "id")   return std::to_string(row.id);
        if (col == "age")  return std::to_string(row.age);
        throw std::runtime_error("Unknown column: " + col);
    }

    bool eval(const Employee &row) const override {
        /* decide int or string comparison */
        bool isIntCol = (col == "id" || col == "age");
        if (isIntCol) {
            int lhs = intVal(row);
            int rhs = std::stoi(val);
            switch (op) {
            case TK::EQ: return lhs == rhs;
            case TK::LT: return lhs <  rhs;
            case TK::GT: return lhs >  rhs;
            case TK::LE: return lhs <= rhs;
            case TK::GE: return lhs >= rhs;
            case TK::NE: return lhs != rhs;
            default: return false;
            }
        } else {
            std::string lhs = strVal(row);
            switch (op) {
            case TK::EQ: return lhs == val;
            case TK::NE: return lhs != val;
            default:
                throw std::runtime_error("String columns only support = and !=");
            }
        }
    }

    std::string str() const override {
        std::string opStr;
        switch (op) {
        case TK::EQ: opStr = "=";  break;
        case TK::LT: opStr = "<";  break;
        case TK::GT: opStr = ">";  break;
        case TK::LE: opStr = "<="; break;
        case TK::GE: opStr = ">="; break;
        case TK::NE: opStr = "!="; break;
        default: opStr = "?";
        }
        return col + " " + opStr + " " + val;
    }
};

/* AND node */
struct And : Expr {
    ExprPtr lhs, rhs;
    And(ExprPtr l, ExprPtr r) : lhs(std::move(l)), rhs(std::move(r)) {}
    bool eval(const Employee &row) const override {
        return lhs->eval(row) && rhs->eval(row);
    }
    std::string str() const override {
        return "(" + lhs->str() + " AND " + rhs->str() + ")";
    }
};

/* OR node */
struct Or : Expr {
    ExprPtr lhs, rhs;
    Or(ExprPtr l, ExprPtr r) : lhs(std::move(l)), rhs(std::move(r)) {}
    bool eval(const Employee &row) const override {
        return lhs->eval(row) || rhs->eval(row);
    }
    std::string str() const override {
        return "(" + lhs->str() + " OR " + rhs->str() + ")";
    }
};

/* NOT node */
struct Not : Expr {
    ExprPtr child;
    explicit Not(ExprPtr c) : child(std::move(c)) {}
    bool eval(const Employee &row) const override { return !child->eval(row); }
    std::string str() const override { return "(NOT " + child->str() + ")"; }
};

/* ===========================================================================
 *  Recursive-descent parser
 *
 *  Grammar (operator precedence encoded in call depth):
 *    expr   -> term  (OR term)*       -- OR has lowest precedence
 *    term   -> factor (AND factor)*   -- AND is tighter than OR
 *    factor -> NOT factor | ( expr ) | comparison
 * =========================================================================== */

class Parser {
public:
    explicit Parser(std::vector<Token> toks) : tokens_(std::move(toks)), pos_(0) {}

    /* Parse a full query: SELECT * FROM table WHERE expr */
    std::pair<std::string, ExprPtr> parse_query()
    {
        consume(TK::KW_SELECT);
        consume(TK::STAR);
        consume(TK::KW_FROM);
        std::string table = cur().text;
        advance();
        consume(TK::KW_WHERE);
        ExprPtr where = parse_expr();
        return {table, std::move(where)};
    }

private:
    std::vector<Token> tokens_;
    size_t             pos_;

    const Token &cur() const { return tokens_[pos_]; }

    void advance() {
        if (cur().kind != TK::END) ++pos_;
    }

    void consume(TK kind) {
        if (cur().kind != kind)
            throw std::runtime_error("Expected token, got: " + cur().text);
        advance();
    }

    bool match(TK kind) {
        if (cur().kind == kind) { advance(); return true; }
        return false;
    }

    /* expr -> term (OR term)* */
    ExprPtr parse_expr()
    {
        ExprPtr node = parse_term();
        while (cur().kind == TK::KW_OR) {
            advance();
            node = std::make_unique<Or>(std::move(node), parse_term());
        }
        return node;
    }

    /* term -> factor (AND factor)* */
    ExprPtr parse_term()
    {
        ExprPtr node = parse_factor();
        while (cur().kind == TK::KW_AND) {
            advance();
            node = std::make_unique<And>(std::move(node), parse_factor());
        }
        return node;
    }

    /* factor -> NOT factor | ( expr ) | comparison */
    ExprPtr parse_factor()
    {
        if (cur().kind == TK::KW_NOT) {
            advance();
            return std::make_unique<Not>(parse_factor());
        }
        if (cur().kind == TK::LPAREN) {
            advance();
            ExprPtr e = parse_expr();
            consume(TK::RPAREN);
            return e;
        }
        return parse_comparison();
    }

    /* comparison -> NAME op (INT_LIT | STR_LIT | NAME) */
    ExprPtr parse_comparison()
    {
        if (cur().kind != TK::NAME)
            throw std::runtime_error("Expected column name, got: " + cur().text);
        std::string col = cur().text;
        advance();

        TK op = cur().kind;
        if (op != TK::EQ && op != TK::LT && op != TK::GT &&
            op != TK::LE && op != TK::GE && op != TK::NE)
            throw std::runtime_error("Expected comparison operator, got: " + cur().text);
        advance();

        std::string val = cur().text;
        if (cur().kind != TK::INT_LIT && cur().kind != TK::STR_LIT &&
            cur().kind != TK::NAME)
            throw std::runtime_error("Expected value, got: " + cur().text);
        advance();

        return std::make_unique<Cmp>(col, op, val);
    }
};

/* ===========================================================================
 *  Query runner
 * =========================================================================== */

static void run_query(const std::string &sql)
{
    std::cout << "\n  SQL : " << sql << "\n";

    /* Lex */
    auto tokens = tokenize(sql);

    /* Parse */
    Parser parser(std::move(tokens));
    auto [table, where_expr] = parser.parse_query();

    std::cout << "  AST : " << where_expr->str() << "\n";
    std::cout << "  ----+----------+-----+-------------\n";
    std::cout << "   id | name     | age | dept\n";
    std::cout << "  ----+----------+-----+-------------\n";

    int count = 0;
    for (const auto &row : EMPLOYEES) {
        if (where_expr->eval(row)) {
            std::cout << "  " << std::setw(3) << row.id << " | "
                      << std::left << std::setw(8) << row.name << " | "
                      << std::right << std::setw(3) << row.age << " | "
                      << row.dept << "\n";
            ++count;
        }
    }
    std::cout << "  ----+----------+-----+-------------\n";
    std::cout << "  " << count << " row(s) matched\n";
}

#include <iomanip>

/* ===========================================================================
 *  main
 * =========================================================================== */

int main()
{
    std::cout << "============================================================\n";
    std::cout << "  Lab 7 -- Part A: SQL Query Parser\n";
    std::cout << "============================================================\n";

    /* Show the table */
    std::cout << "\n  Table: employees (10 rows)\n";
    std::cout << "  ----+----------+-----+-------------\n";
    std::cout << "   id | name     | age | dept\n";
    std::cout << "  ----+----------+-----+-------------\n";
    for (const auto &r : EMPLOYEES) {
        std::cout << "  " << std::setw(3) << r.id << " | "
                  << std::left  << std::setw(8) << r.name << " | "
                  << std::right << std::setw(3) << r.age  << " | "
                  << r.dept << "\n";
    }
    std::cout << "  ----+----------+-----+-------------\n";

    /* Demonstrate the pipeline: SQL -> tokens -> AST -> results */
    std::cout << "\n============================================================\n";
    std::cout << "  Query demonstrations\n";
    std::cout << "============================================================\n";

    run_query("SELECT * FROM employees WHERE age > 28");
    run_query("SELECT * FROM employees WHERE age > 25 AND age < 35");
    run_query("SELECT * FROM employees WHERE dept = Engineering");
    run_query("SELECT * FROM employees WHERE age < 30 AND dept = Engineering");
    run_query("SELECT * FROM employees WHERE id = 1 OR id = 5 OR id = 9");
    run_query("SELECT * FROM employees WHERE (age > 30 OR dept = HR) AND age <= 40");
    run_query("SELECT * FROM employees WHERE NOT dept = Marketing");

    std::cout << "\n============================================================\n";
    std::cout << "  Tokenization trace for last query\n";
    std::cout << "============================================================\n";

    std::string sql = "SELECT * FROM employees WHERE NOT dept = Marketing";
    auto tokens = tokenize(sql);
    std::cout << "\n  Source: " << sql << "\n\n";
    std::cout << "  Token stream:\n";
    for (const auto &t : tokens) {
        std::string kind;
        switch (t.kind) {
        case TK::KW_SELECT: kind = "KW_SELECT"; break;
        case TK::KW_FROM:   kind = "KW_FROM";   break;
        case TK::KW_WHERE:  kind = "KW_WHERE";  break;
        case TK::KW_AND:    kind = "KW_AND";    break;
        case TK::KW_OR:     kind = "KW_OR";     break;
        case TK::KW_NOT:    kind = "KW_NOT";    break;
        case TK::STAR:      kind = "STAR";       break;
        case TK::NAME:      kind = "NAME";       break;
        case TK::INT_LIT:   kind = "INT_LIT";   break;
        case TK::STR_LIT:   kind = "STR_LIT";   break;
        case TK::EQ:        kind = "EQ";         break;
        case TK::LT:        kind = "LT";         break;
        case TK::GT:        kind = "GT";         break;
        case TK::LE:        kind = "LE";         break;
        case TK::GE:        kind = "GE";         break;
        case TK::NE:        kind = "NE";         break;
        case TK::LPAREN:    kind = "LPAREN";     break;
        case TK::RPAREN:    kind = "RPAREN";     break;
        case TK::END:       kind = "END";        break;
        }
        std::cout << "    " << std::left << std::setw(12) << kind
                  << " \"" << t.text << "\"\n";
    }

    std::cout << "\n============================================================\n";
    std::cout << "  Done.\n";
    std::cout << "============================================================\n";

    return 0;
}
