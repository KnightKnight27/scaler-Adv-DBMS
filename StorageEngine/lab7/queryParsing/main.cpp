// Lab 7 — Query Parsing (tokenize → AST → execute)
// Author: 24BCS10183 Aman Yadav  (Class B, 2nd year)
//
// Reads a simple `SELECT col FROM table WHERE ...` string, tokenizes it,
// recursive-descent parses it into an AST (SelectStatement + an Expression
// tree of ColumnRef / Literal / BinaryExpression), then executes the AST
// against an in-memory table of Employees and prints matching rows.

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

struct Employee {
    string name;
    int    id;
    int    age;

    int getInt(const string& col) const {
        if (col == "id")  return id;
        if (col == "age") return age;
        throw runtime_error("Unknown int column: " + col);
    }

    string getString(const string& col) const {
        if (col == "name") return name;
        throw runtime_error("Unknown string column: " + col);
    }
};

enum class TokenType {
    SELECT,
    FROM,
    WHERE,
    OR,
    IDENTIFIER,
    NUMBER,
    GT,
    LT,
    EQ,
    GTE,
    LTE,
    LPAREN,
    RPAREN,
    END
};

struct Token {
    TokenType type;
    string    text;
};

// ---------------------------------------------------------------------------
// Lexer — turns the raw SQL string into a stream of tokens.
// ---------------------------------------------------------------------------
class Lexer {
public:
    explicit Lexer(const string& sql) : input(sql) {}

    vector<Token> tokenize() {
        vector<Token> tokens;
        size_t pos = 0;
        while (pos < input.size()) {
            if (isspace(static_cast<unsigned char>(input[pos]))) {
                ++pos;
                continue;
            }

            if (isalpha(static_cast<unsigned char>(input[pos]))) {
                string word;
                while (pos < input.size() &&
                       (isalnum(static_cast<unsigned char>(input[pos])) ||
                        input[pos] == '_')) {
                    word += input[pos++];
                }
                string upper = word;
                transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                if      (upper == "SELECT") tokens.push_back({TokenType::SELECT, word});
                else if (upper == "FROM")   tokens.push_back({TokenType::FROM,   word});
                else if (upper == "WHERE")  tokens.push_back({TokenType::WHERE,  word});
                else if (upper == "OR")     tokens.push_back({TokenType::OR,     word});
                else                        tokens.push_back({TokenType::IDENTIFIER, word});
            } else if (isdigit(static_cast<unsigned char>(input[pos]))) {
                string num;
                while (pos < input.size() &&
                       isdigit(static_cast<unsigned char>(input[pos]))) {
                    num += input[pos++];
                }
                tokens.push_back({TokenType::NUMBER, num});
            } else if (input[pos] == '>') {
                if (pos + 1 < input.size() && input[pos + 1] == '=') {
                    tokens.push_back({TokenType::GTE, ">="}); pos += 2;
                } else {
                    tokens.push_back({TokenType::GT, ">"});   ++pos;
                }
            } else if (input[pos] == '<') {
                if (pos + 1 < input.size() && input[pos + 1] == '=') {
                    tokens.push_back({TokenType::LTE, "<="}); pos += 2;
                } else {
                    tokens.push_back({TokenType::LT, "<"});   ++pos;
                }
            } else if (input[pos] == '=') {
                tokens.push_back({TokenType::EQ, "="});       ++pos;
            } else if (input[pos] == '(') {
                tokens.push_back({TokenType::LPAREN, "("});   ++pos;
            } else if (input[pos] == ')') {
                tokens.push_back({TokenType::RPAREN, ")"});   ++pos;
            } else {
                ++pos;
            }
        }
        tokens.push_back({TokenType::END, ""});
        return tokens;
    }

private:
    string input;
};

// ---------------------------------------------------------------------------
// AST node hierarchy.
// ---------------------------------------------------------------------------
struct Expression {
    virtual ~Expression() = default;
};

struct Literal : Expression {
    int value;
    explicit Literal(int v) : value(v) {}
};

struct ColumnRef : Expression {
    string name;
    explicit ColumnRef(string n) : name(std::move(n)) {}
};

struct BinaryExpression : Expression {
    string      op;
    Expression* left;
    Expression* right;
    BinaryExpression(string o, Expression* l, Expression* r)
        : op(std::move(o)), left(l), right(r) {}
};

struct SelectStatement {
    string      column;
    string      tableName;
    Expression* whereFilter;
};

// ---------------------------------------------------------------------------
// Parser — recursive descent.
//
//   select     := SELECT IDENT FROM IDENT WHERE expression
//   expression := primary ( OR primary )*
//   primary    := '(' expression ')' | condition
//   condition  := IDENT (> | < | >= | <=) NUMBER
// ---------------------------------------------------------------------------
class DbParser {
public:
    explicit DbParser(vector<Token> toks) : tokens(std::move(toks)) {}

    SelectStatement parseSelect() {
        consume(TokenType::SELECT);
        string column = consume(TokenType::IDENTIFIER).text;
        consume(TokenType::FROM);
        string table = consume(TokenType::IDENTIFIER).text;
        consume(TokenType::WHERE);
        Expression* where = parseExpression();
        return SelectStatement{column, table, where};
    }

private:
    Expression* parseExpression() {
        Expression* left = parsePrimary();
        while (tokens[pos].type == TokenType::OR) {
            consume(TokenType::OR);
            Expression* right = parsePrimary();
            left = new BinaryExpression("OR", left, right);
        }
        return left;
    }

    Expression* parsePrimary() {
        if (tokens[pos].type == TokenType::LPAREN) {
            consume(TokenType::LPAREN);
            Expression* expr = parseExpression();
            consume(TokenType::RPAREN);
            return expr;
        }
        return parseCondition();
    }

    Expression* parseCondition() {
        string      column   = consume(TokenType::IDENTIFIER).text;
        Expression* leftExpr = new ColumnRef(column);

        string op;
        if      (tokens[pos].type == TokenType::GTE) { op = ">="; consume(TokenType::GTE); }
        else if (tokens[pos].type == TokenType::LTE) { op = "<="; consume(TokenType::LTE); }
        else if (tokens[pos].type == TokenType::GT)  { op = ">";  consume(TokenType::GT);  }
        else if (tokens[pos].type == TokenType::LT)  { op = "<";  consume(TokenType::LT);  }
        else if (tokens[pos].type == TokenType::EQ)  { op = "=";  consume(TokenType::EQ);  }
        else throw runtime_error("Expected one of: >, <, >=, <=, =");

        int         value     = stoi(consume(TokenType::NUMBER).text);
        Expression* rightExpr = new Literal(value);
        return new BinaryExpression(op, leftExpr, rightExpr);
    }

    Token consume(TokenType expected) {
        if (tokens[pos].type != expected)
            throw runtime_error("invalid token at position " + to_string(pos)
                                + " (got '" + tokens[pos].text + "')");
        return tokens[pos++];
    }

    vector<Token> tokens;
    size_t        pos = 0;
};

// ---------------------------------------------------------------------------
// Executor — walks the AST against each row.
// ---------------------------------------------------------------------------
int getInt(Expression* expr, const Employee& row) {
    if (auto* col = dynamic_cast<ColumnRef*>(expr)) return row.getInt(col->name);
    if (auto* lit = dynamic_cast<Literal*>(expr))   return lit->value;
    throw runtime_error("getInt: unsupported expression node");
}

bool applyFilter(Expression* expr, const Employee& row) {
    auto* bin = dynamic_cast<BinaryExpression*>(expr);
    if (!bin) throw runtime_error("filter expression not a BinaryExpression");

    if (bin->op == "OR")
        return applyFilter(bin->left, row) || applyFilter(bin->right, row);

    int left  = getInt(bin->left,  row);
    int right = getInt(bin->right, row);
    if (bin->op == ">")  return left >  right;
    if (bin->op == "<")  return left <  right;
    if (bin->op == ">=") return left >= right;
    if (bin->op == "<=") return left <= right;
    if (bin->op == "=")  return left == right;
    throw runtime_error("unknown comparison operator: " + bin->op);
}

void execute(const SelectStatement& statement, const vector<Employee>& employees) {
    for (const auto& row : employees) {
        if (!applyFilter(statement.whereFilter, row)) continue;
        if      (statement.column == "name") cout << row.getString("name") << '\n';
        else if (statement.column == "id")   cout << row.getInt("id")      << '\n';
        else if (statement.column == "age")  cout << row.getInt("age")     << '\n';
    }
}

int main() {
    vector<Employee> employees = {
        {"Aman",   1, 19},
        {"Riya",   2, 20},
        {"Karan",  3, 19},
        {"Sneha",  4, 21},
        {"Vivaan", 5, 20},
        {"Ishaan", 6, 22},
    };

    string sqlQuery = "SELECT name FROM employees WHERE id >= 3";

    cout << "Query: " << sqlQuery << "\n";
    cout << "Matching rows:\n";

    Lexer         lexer(sqlQuery);
    vector<Token> tokens = lexer.tokenize();

    DbParser        dbParser(tokens);
    SelectStatement statement = dbParser.parseSelect();

    execute(statement, employees);
    return 0;
}
