#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// Row representation: column_name -> value
using Row = std::unordered_map<std::string, std::string>;

// AST Node types for expressions
struct Expr {
    virtual ~Expr() = default;
    virtual std::string evaluate(const Row &row) = 0;
};

struct ColumnExpr : Expr {
    std::string name;
    ColumnExpr(const std::string &n) : name(n) {
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    }
    std::string evaluate(const Row &row) override {
        auto it = row.find(name);
        return it != row.end() ? it->second : "";
    }
};

struct LiteralExpr : Expr {
    std::string value;
    LiteralExpr(const std::string &v) : value(v) {}
    std::string evaluate(const Row &) override { return value; }
};

struct BinaryOpExpr : Expr {
    std::shared_ptr<Expr> left;
    std::shared_ptr<Expr> right;
    std::string op;

    BinaryOpExpr(std::shared_ptr<Expr> l, std::shared_ptr<Expr> r, const std::string &o)
        : left(l), right(r), op(o) {}

    std::string evaluate(const Row &row) override {
        std::string lval = left->evaluate(row);
        std::string rval = right->evaluate(row);

        if (op == "=") {
            return lval == rval ? "true" : "false";
        }

        try {
            double lnum = std::stod(lval);
            double rnum = std::stod(rval);

            if (op == "<") {
                return lnum < rnum ? "true" : "false";
            } else if (op == ">") {
                return lnum > rnum ? "true" : "false";
            } else if (op == "<=") {
                return lnum <= rnum ? "true" : "false";
            } else if (op == ">=") {
                return lnum >= rnum ? "true" : "false";
            } else if (op == "!=") {
                return lnum != rnum ? "true" : "false";
            }
        } catch (...) {
            return "false";
        }
        return "";
    }
};

// Tokenizer
struct Token {
    enum Type { KEYWORD, IDENTIFIER, NUMBER, OPERATOR, LPAREN, RPAREN, COMMA, END };
    Type type;
    std::string value;

    Token(Type t = END, const std::string &v = "") : type(t), value(v) {}
};

class Lexer {
private:
    std::string input;
    std::size_t pos;

    bool isKeyword(const std::string &s) {
        return s == "SELECT" || s == "FROM" || s == "WHERE";
    }

public:
    Lexer(const std::string &s) : input(s), pos(0) {
        std::transform(input.begin(), input.end(), input.begin(), ::toupper);
    }

    Token nextToken() {
        while (pos < input.length() && std::isspace(input[pos])) ++pos;
        if (pos >= input.length()) return Token(Token::END);

        if (input[pos] == '(') {
            ++pos;
            return Token(Token::LPAREN, "(");
        }
        if (input[pos] == ')') {
            ++pos;
            return Token(Token::RPAREN, ")");
        }
        if (input[pos] == ',') {
            ++pos;
            return Token(Token::COMMA, ",");
        }

        if (std::isdigit(input[pos])) {
            std::string num;
            while (pos < input.length() && std::isdigit(input[pos])) {
                num += input[pos++];
            }
            return Token(Token::NUMBER, num);
        }

        if (std::isalpha(input[pos]) || input[pos] == '_') {
            std::string ident;
            while (pos < input.length() && (std::isalnum(input[pos]) || input[pos] == '_')) {
                ident += input[pos++];
            }
            if (isKeyword(ident)) {
                return Token(Token::KEYWORD, ident);
            }
            return Token(Token::IDENTIFIER, ident);
        }

        if (input[pos] == '=' || input[pos] == '<' || input[pos] == '>' || input[pos] == '!') {
            std::string op;
            op += input[pos++];
            if (pos < input.length() && input[pos] == '=') {
                op += input[pos++];
            }
            return Token(Token::OPERATOR, op);
        }

        ++pos;
        return nextToken();
    }
};

// Parser
class Parser {
private:
    std::vector<Token> tokens;
    std::size_t pos;

    Token current() { return pos < tokens.size() ? tokens[pos] : Token(Token::END); }
    void advance() {
        if (pos < tokens.size()) ++pos;
    }
    void expect(Token::Type type) {
        if (current().type != type) throw std::runtime_error("Unexpected token");
        advance();
    }

public:
    Parser(const std::string &input) : pos(0) {
        Lexer lexer(input);
        Token t;
        do {
            t = lexer.nextToken();
            tokens.push_back(t);
        } while (t.type != Token::END);
    }

    struct SelectStmt {
        std::vector<std::string> columns;
        std::string table;
        std::shared_ptr<Expr> whereClause;
    };

    SelectStmt parse() {
        SelectStmt stmt;
        expect(Token::KEYWORD);

        while (current().type != Token::KEYWORD || current().value != "FROM") {
            if (current().type == Token::IDENTIFIER) {
                std::string col = current().value;
                std::transform(col.begin(), col.end(), col.begin(), ::tolower);
                stmt.columns.push_back(col);
                advance();
            } else if (current().type == Token::COMMA) {
                advance();
            } else {
                advance();
            }
        }

        expect(Token::KEYWORD);

        if (current().type == Token::IDENTIFIER) {
            stmt.table = current().value;
            advance();
        }

        if (current().type == Token::KEYWORD && current().value == "WHERE") {
            advance();
            stmt.whereClause = parseExpression();
        }

        return stmt;
    }

private:
    std::shared_ptr<Expr> parseExpression() {
        auto left = parsePrimary();

        while (current().type == Token::OPERATOR) {
            std::string op = current().value;
            advance();
            auto right = parsePrimary();
            left = std::make_shared<BinaryOpExpr>(left, right, op);
        }

        return left;
    }

    std::shared_ptr<Expr> parsePrimary() {
        if (current().type == Token::IDENTIFIER) {
            std::string name = current().value;
            advance();
            return std::make_shared<ColumnExpr>(name);
        }

        if (current().type == Token::NUMBER) {
            std::string value = current().value;
            advance();
            return std::make_shared<LiteralExpr>(value);
        }

        throw std::runtime_error("Unexpected token in expression");
    }
};

int main() {
    std::vector<Row> table = {
        {{"name", "Alice"}, {"age", "30"}, {"salary", "50000"}},
        {{"name", "Bob"}, {"age", "25"}, {"salary", "45000"}},
        {{"name", "Charlie"}, {"age", "35"}, {"salary", "60000"}},
        {{"name", "David"}, {"age", "28"}, {"salary", "48000"}},
    };

    std::cout << "=== Original Table ===\n";
    for (const auto &row : table) {
        for (const auto &[k, v] : row) {
            std::cout << k << "=" << v << " ";
        }
        std::cout << '\n';
    }

    std::string query = "SELECT name, age FROM table WHERE age >= 30";
    std::cout << "\nQuery: " << query << "\n\n";

    Parser parser(query);
    auto stmt = parser.parse();

    std::cout << "=== Query Result ===\n";
    std::cout << "Columns: ";
    for (const auto &col : stmt.columns) {
        std::cout << col << " ";
    }
    std::cout << "\n\n";

    for (const auto &row : table) {
        if (stmt.whereClause && stmt.whereClause->evaluate(row) == "false") {
            continue;
        }

        for (const auto &col : stmt.columns) {
            auto it = row.find(col);
            if (it != row.end()) {
                std::cout << it->second << " ";
            }
        }
        std::cout << '\n';
    }

    return 0;
}
