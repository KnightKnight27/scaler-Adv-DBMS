#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <memory>
#include <stdexcept>

using namespace std;

enum class Tok {
    Select, From, Where, And, Or,
    Ident, Number,
    Gt, Lt, Eq, Gte, Lte,
    LParen, RParen,
    End,
};

struct Token {
    Tok kind;
    string text;
};

static string toUpper(const string& s) {
    string r;
    r.reserve(s.size());
    for (char c : s) r.push_back((char)toupper((unsigned char)c));
    return r;
}

static vector<Token> lex(const string& src) {
    vector<Token> out;
    size_t i = 0;
    while (i < src.size()) {
        char c = src[i];
        if (isspace((unsigned char)c)) { ++i; continue; }

        if (isalpha((unsigned char)c) || c == '_') {
            size_t j = i;
            while (j < src.size() && (isalnum((unsigned char)src[j]) || src[j] == '_')) ++j;
            string word = src.substr(i, j - i);
            string up = toUpper(word);
            i = j;
            if      (up == "SELECT") out.push_back({Tok::Select, word});
            else if (up == "FROM")   out.push_back({Tok::From,   word});
            else if (up == "WHERE")  out.push_back({Tok::Where,  word});
            else if (up == "AND")    out.push_back({Tok::And,    word});
            else if (up == "OR")     out.push_back({Tok::Or,     word});
            else                     out.push_back({Tok::Ident,  word});
            continue;
        }

        if (isdigit((unsigned char)c)) {
            size_t j = i;
            while (j < src.size() && isdigit((unsigned char)src[j])) ++j;
            out.push_back({Tok::Number, src.substr(i, j - i)});
            i = j;
            continue;
        }

        if ((c == '>' || c == '<') && i + 1 < src.size() && src[i + 1] == '=') {
            out.push_back({c == '>' ? Tok::Gte : Tok::Lte, string() + c + '='});
            i += 2;
            continue;
        }

        switch (c) {
            case '>': out.push_back({Tok::Gt, ">"});      ++i; break;
            case '<': out.push_back({Tok::Lt, "<"});      ++i; break;
            case '=': out.push_back({Tok::Eq, "="});      ++i; break;
            case '(': out.push_back({Tok::LParen, "("});  ++i; break;
            case ')': out.push_back({Tok::RParen, ")"});  ++i; break;
            default:
                throw runtime_error(string("unexpected char: ") + c);
        }
    }
    out.push_back({Tok::End, ""});
    return out;
}

enum class Node {
    Column,
    Number,
    BinOp,
};

struct Expr {
    Node kind;
    string text;
    unique_ptr<Expr> lhs;
    unique_ptr<Expr> rhs;
};

static unique_ptr<Expr> mkCol(const string& n) { return unique_ptr<Expr>(new Expr{Node::Column, n, nullptr, nullptr}); }
static unique_ptr<Expr> mkNum(const string& n) { return unique_ptr<Expr>(new Expr{Node::Number, n, nullptr, nullptr}); }
static unique_ptr<Expr> mkBin(const string& op, unique_ptr<Expr> a, unique_ptr<Expr> b) {
    return unique_ptr<Expr>(new Expr{Node::BinOp, op, std::move(a), std::move(b)});
}

struct Select {
    string column;
    string table;
    unique_ptr<Expr> where;
};

// Grammar:
//   select := SELECT IDENT FROM IDENT WHERE expr
//   expr   := term ( OR term )*
//   term   := factor ( AND factor )*
//   factor := '(' expr ')' | cmp
//   cmp    := IDENT (>|<|>=|<=|=) NUMBER
class Parser {
public:
    explicit Parser(vector<Token> toks) : tokens(std::move(toks)) {}

    Select parseSelect() {
        eat(Tok::Select);
        string col = eat(Tok::Ident).text;
        eat(Tok::From);
        string tbl = eat(Tok::Ident).text;
        eat(Tok::Where);
        auto w = parseExpr();
        eat(Tok::End);
        return Select{col, tbl, std::move(w)};
    }

private:
    unique_ptr<Expr> parseExpr() {
        auto left = parseTerm();
        while (tokens[pos].kind == Tok::Or) {
            pos++;
            auto right = parseTerm();
            left = mkBin("OR", std::move(left), std::move(right));
        }
        return left;
    }

    unique_ptr<Expr> parseTerm() {
        auto left = parseFactor();
        while (tokens[pos].kind == Tok::And) {
            pos++;
            auto right = parseFactor();
            left = mkBin("AND", std::move(left), std::move(right));
        }
        return left;
    }

    unique_ptr<Expr> parseFactor() {
        if (tokens[pos].kind == Tok::LParen) {
            pos++;
            auto e = parseExpr();
            eat(Tok::RParen);
            return e;
        }
        return parseCmp();
    }

    unique_ptr<Expr> parseCmp() {
        string col = eat(Tok::Ident).text;
        string op;
        switch (tokens[pos].kind) {
            case Tok::Gt:  op = ">";  break;
            case Tok::Lt:  op = "<";  break;
            case Tok::Gte: op = ">="; break;
            case Tok::Lte: op = "<="; break;
            case Tok::Eq:  op = "=";  break;
            default: throw runtime_error("expected comparison after " + col);
        }
        pos++;
        string num = eat(Tok::Number).text;
        return mkBin(op, mkCol(col), mkNum(num));
    }

    Token eat(Tok expected) {
        if (tokens[pos].kind != expected)
            throw runtime_error("unexpected token: " + tokens[pos].text);
        return tokens[pos++];
    }

    vector<Token> tokens;
    size_t pos = 0;
};

struct Student {
    int    id;
    string name;
    int    marks;
};

static int columnValue(const string& col, const Student& s) {
    if (col == "id")    return s.id;
    if (col == "marks") return s.marks;
    throw runtime_error("unknown column: " + col);
}

static bool matches(const Expr* e, const Student& row) {
    if (e->text == "AND") return matches(e->lhs.get(), row) && matches(e->rhs.get(), row);
    if (e->text == "OR")  return matches(e->lhs.get(), row) || matches(e->rhs.get(), row);

    int lv = columnValue(e->lhs->text, row);
    int rv = stoi(e->rhs->text);
    if (e->text == ">")  return lv >  rv;
    if (e->text == "<")  return lv <  rv;
    if (e->text == ">=") return lv >= rv;
    if (e->text == "<=") return lv <= rv;
    if (e->text == "=")  return lv == rv;
    throw runtime_error("unknown operator: " + e->text);
}

static void run(const Select& q, const vector<Student>& rows) {
    cout << "Result of: SELECT " << q.column << " FROM " << q.table << " WHERE ...\n";
    for (const Student& row : rows) {
        if (!matches(q.where.get(), row)) continue;
        if      (q.column == "name")  cout << "  " << row.name  << "\n";
        else if (q.column == "id")    cout << "  " << row.id    << "\n";
        else if (q.column == "marks") cout << "  " << row.marks << "\n";
    }
}

int main() {
    vector<Student> students = {
        {1, "Aarav",   78},
        {2, "Diya",    91},
        {3, "Kabir",   55},
        {4, "Meera",   88},
        {5, "Rohan",   42},
        {6, "Sneha",   95},
    };

    string sql = "SELECT name FROM students WHERE marks >= 80 AND id < 6";

    cout << "Query: " << sql << "\n\n";

    Parser parser(lex(sql));
    Select stmt = parser.parseSelect();
    run(stmt, students);

    return 0;
}
