#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>

using namespace std;

struct Employee {
    string name;
    int id;
    int age;

    // resolve a column name to its value on this row
    int getInt(const string& col) const {
        if (col == "id")  return id;
        if (col == "age") return age;
        throw runtime_error("Unknown column for int: " + col);
    }

    string getString(const string& col) const {
        if (col == "name") return name;
        throw runtime_error("Unknown column for string: " + col);
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
    END,
    OPERATOR,
    INTEGER_LITERAL,
    KEYWORD
};

struct Token {
    TokenType type;
    string text; 
};

class Parser {
private:
    string input;
public:
    Parser(string& sqlQuery) {
        input = sqlQuery;
    }
    vector<Token> tokenize() {
        vector<Token> tokens;
        int pos = 0;
        while (pos < input.size()) {
            if (isspace(input[pos])) {
                ++pos;
                continue;
            }

            if (isalpha(input[pos])) {
                std::string word;

                while (pos < input.size() &&
                       (isalnum(input[pos]) ||
                        input[pos] == '_')) {
                    word += input[pos++];
                }

                string upper = word;
                transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                if (upper == "SELECT")
                    tokens.push_back({TokenType::SELECT, word});
                else if (upper == "FROM")
                    tokens.push_back({TokenType::FROM, word});
                else if (upper == "WHERE")
                    tokens.push_back({TokenType::WHERE, word});
                else if (upper == "OR")
                    tokens.push_back({TokenType::OR, word});
                else
                    tokens.push_back({TokenType::IDENTIFIER, word});
            } else if (isdigit(input[pos])) {
                std::string num;
                while (pos < input.size() &&
                       isdigit(input[pos])) {
                    num += input[pos++];
                }

                tokens.push_back({TokenType::NUMBER, num});
            } else if (input[pos] == '>') {
                if (pos + 1 < input.size() && input[pos + 1] == '=') {
                    tokens.push_back({TokenType::GTE, ">="});
                    pos += 2;
                } else {
                    tokens.push_back({TokenType::GT, ">"});
                    ++pos;
                }
            } else if (input[pos] == '<') {
                if (pos + 1 < input.size() && input[pos + 1] == '=') {
                    tokens.push_back({TokenType::LTE, "<="});
                    pos += 2;
                } else {
                    tokens.push_back({TokenType::LT, "<"});
                    ++pos;
                }
            } else if (input[pos] == '=') {
                tokens.push_back({TokenType::EQ, "="});
                ++pos;
            } else if (input[pos] == '(') {
                tokens.push_back({TokenType::LPAREN, "("});
                ++pos;
            } else if (input[pos] == ')') {
                tokens.push_back({TokenType::RPAREN, ")"});
                ++pos;
            } else {
                ++pos;
            }
        }
        tokens.push_back({TokenType::END, ""});
        return tokens;
    }
};

struct Expression {
    virtual ~Expression() = default;
};

struct Literal : Expression {
    int value;
    Literal(int val) { value = val; }
};

struct ColumnRef : Expression {
    string name;
    ColumnRef(string na) : name(na) {}
};

struct BinaryExpression : Expression {
    string op;
    Expression* left;
    Expression* right;
    BinaryExpression(string op, Expression* left, Expression* right)
        : op(op), left(left), right(right) {}
};

struct SelectStatement {
    string column;
    string tableName;
    Expression* whereFilter;
};

class DbParser {
public:
    DbParser(vector<Token> toks) : tokens(toks) {}

    SelectStatement parseSelect() {
        consume(TokenType::SELECT);
        string column = consume(TokenType::IDENTIFIER).text;
        consume(TokenType::FROM);
        string table = consume(TokenType::IDENTIFIER).text;
        consume(TokenType::WHERE);
        Expression* where = parseExpression();

        SelectStatement statement;
        statement.column = column;
        statement.tableName = table;
        statement.whereFilter = where;
        return statement;
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
        string column = consume(TokenType::IDENTIFIER).text;
        Expression* leftExpr = new ColumnRef(column);

        string op;
        if (tokens[pos].type == TokenType::GTE) {
            op = ">="; consume(TokenType::GTE);
        } else if (tokens[pos].type == TokenType::LTE) {
            op = "<="; consume(TokenType::LTE);
        } else if (tokens[pos].type == TokenType::GT) {
            op = ">"; consume(TokenType::GT);
        } else if (tokens[pos].type == TokenType::LT) {
            op = "<"; consume(TokenType::LT);
        } else {
            throw runtime_error("Expected >, <, >= or <=");
        }

        int value = stoi(consume(TokenType::NUMBER).text);
        Expression* rightExpr = new Literal(value);
        return new BinaryExpression(op, leftExpr, rightExpr);
    }

    Token consume(TokenType expected) {
        if (tokens[pos].type != expected)
            throw runtime_error("invalid token format");
        return tokens[pos++];
    }

    vector<Token> tokens;
    int pos = 0;
};

// Resolve an expression node to an int: a column reads from the row,
// a literal returns its own value. Lets applyFilter treat both sides the same.
int getInt(Expression* expr, const Employee& row) {
    if (auto* col = dynamic_cast<ColumnRef*>(expr))
        return row.getInt(col->name);
    if (auto* lit = dynamic_cast<Literal*>(expr))
        return lit->value;
    throw runtime_error("invalid expression in getInt");
}

bool applyFilter(Expression* expr, const Employee& row) {
    auto* bin = dynamic_cast<BinaryExpression*>(expr);
    if (!bin) throw runtime_error("expression not valid");

    if (bin->op == "OR")
        return applyFilter(bin->left, row) || applyFilter(bin->right, row);

    // both sides resolved through getInt — works for column vs literal,
    // literal vs column, or even column vs column
    int left  = getInt(bin->left, row);
    int right = getInt(bin->right, row);
    if (bin->op == ">")  return left > right;
    if (bin->op == "<")  return left < right;
    if (bin->op == ">=") return left >= right;
    if (bin->op == "<=") return left <= right;
    if (bin->op == "=")  return left == right;
    throw runtime_error("invalid operator");
}

void execute(SelectStatement& statement, const vector<Employee>& employees) {
    for (const auto& row : employees) {
        if (applyFilter(statement.whereFilter, row)) {
            if (statement.column == "name")      cout << row.getString("name") << endl;
            else if (statement.column == "id")   cout << row.getInt("id") << endl;
            else if (statement.column == "age")  cout << row.getInt("age") << endl;
        }
    }
}

int main() {
    vector<Employee> employees;
    employees.push_back({"Ankur", 1, 22});
    employees.push_back({"Manjari", 2, 22});
    employees.push_back({"Kartik", 3, 28});
    employees.push_back({"Swapnanil", 4, 24});
    employees.push_back({"Nipu", 5, 22});

    string sqlQuery = "SELECT name from employees where id >= 3";

    Parser parser(sqlQuery);
    vector<Token> tokens = parser.tokenize();

    DbParser dbParser(tokens);
    SelectStatement statement = dbParser.parseSelect();

    // for (auto token : tokens) {
    //     cout << token.text << endl;
    // }

    execute(statement, employees);

    return 0;
}