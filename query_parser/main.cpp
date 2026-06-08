#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <stdexcept>

using namespace std;
// SELECT * FROM SOMETHING
// ------------------- Tokenizer -------------------
enum class TokenType {
    SELECT, FROM, WHERE, OR, IDENTIFIER, NUMBER,
    GT, LT, LPAREN, RPAREN, END
};

struct Token {
    TokenType type;
    string text;
};

class Lexer {
public:
    explicit Lexer(string sql) : input(move(sql)) {
    }

    vector<Token> tokenize() {
        vector<Token> tokens;
        size_t pos = 0;

        while (pos < input.size()) {
            if (isspace(input[pos])) {
                ++pos;
                continue;
            }

            if (isalpha(input[pos])) {
                string word;
                while (pos < input.size() &&
                       (isalnum(input[pos]) || input[pos] == '_')) {
                    word += input[pos++];
                }
                if (word == "SELECT") tokens.push_back({TokenType::SELECT, word});
                else if (word == "FROM") tokens.push_back({TokenType::FROM, word});
                else if (word == "WHERE") tokens.push_back({TokenType::WHERE, word});
                else if (word == "OR") tokens.push_back({TokenType::OR, word});
                else tokens.push_back({TokenType::IDENTIFIER, word});
            } else if (isdigit(input[pos])) {
                string num;
                while (pos < input.size() && isdigit(input[pos])) num += input[pos++];
                tokens.push_back({TokenType::NUMBER, num});
            } else if (input[pos] == '>') {
                tokens.push_back({TokenType::GT, ">"});
                ++pos;
            } else if (input[pos] == '<') {
                tokens.push_back({TokenType::LT, "<"});
                ++pos;
            } else if (input[pos] == '(') {
                tokens.push_back({TokenType::LPAREN, "("});
                ++pos;
            } else if (input[pos] == ')') {
                tokens.push_back({TokenType::RPAREN, ")"});
                ++pos;
            } else { ++pos; }
        }
        tokens.push_back({TokenType::END, ""});
        return tokens;
    }

private:
    string input;
};

// age > 15
//      >
//   18    15

// ------------------- AST -------------------
struct Expression {
    virtual ~Expression() = default;
};

struct Literal : Expression {
    int value;

    explicit Literal(int v) : value(v) {
    }
};

struct ColumnRef : Expression {
    string name;

    explicit ColumnRef(string n) : name(move(n)) {
    }
};

struct BinaryExpression : Expression {
    string op;
    Expression *left;
    Expression *right;

    BinaryExpression(string o, Expression *l, Expression *r)
        : op(move(o)), left(l), right(r) {
    }
};

// ------------------- Select Statement -------------------
struct SelectStatement {
    string column;
    string tableName;
    Expression *whereFilter;
};

// ------------------- Parser -------------------
class DbParser {
public:
    explicit DbParser(vector<Token> toks) : tokens(move(toks)) {
    }

    SelectStatement parseSelect() {
        consume(TokenType::SELECT);
        string column = consume(TokenType::IDENTIFIER).text;
        consume(TokenType::FROM);
        string table = consume(TokenType::IDENTIFIER).text;
        consume(TokenType::WHERE);
        auto where = parseExpression();

        SelectStatement stmt;
        stmt.column = column;
        stmt.tableName = table;
        stmt.whereFilter = where;
        return stmt;
    }

private:
    vector<Token> tokens;
    size_t pos = 0;

    Token &current() { return tokens[pos]; }

    Token consume(TokenType expected) {
        if (current().type != expected) throw runtime_error("Unexpected token");
        return tokens[pos++];
    }

    Expression *parseExpression() {
        auto left = parsePrimary();
        while (current().type == TokenType::OR) {
            consume(TokenType::OR);
            auto right = parsePrimary();
            left = new BinaryExpression("OR", left, right);
        }
        return left;
    }

    Expression *parsePrimary() {
        if (current().type == TokenType::LPAREN) {
            consume(TokenType::LPAREN);
            auto expr = parseExpression();
            consume(TokenType::RPAREN);
            return expr;
        }
        return parseCondition();
    }

    Expression *parseCondition() {
        // age > 25
        string col = consume(TokenType::IDENTIFIER).text;
        Expression *left = new ColumnRef(col);

        string op;
        if (current().type == TokenType::GT) {
            op = ">";
            consume(TokenType::GT);
        } else if (current().type == TokenType::LT) {
            op = "<";
            consume(TokenType::LT);
        } else throw runtime_error("Expected > or <");

        int value = stoi(consume(TokenType::NUMBER).text);
        Expression *right = new Literal(value);

        return new BinaryExpression(op, left, right);
    }
};
// variant pointer to r type
// ------------------- Employee -------------------
struct Employee {
    string name;
    int id;
    int age;

    int getInt(const string &col) const {
        if (col == "id") return id;
        if (col == "age") return age;
        throw runtime_error("Unknown column for int: " + col);
    }

    string getString(const string &col) const {
        if (col == "name") return name;
        throw runtime_error("Unknown column for string: " + col);
    }
};

// ------------------- Evaluator -------------------

int getInt(Expression* expr, const Employee& row) {
    auto * columnPtr = dynamic_cast<ColumnRef*>(expr);
    if (columnPtr) {
        if (columnPtr->name == "age")
            return row.age;
        else if (columnPtr->name == "id")
            return row.id;
    }

    auto * literalPtr = dynamic_cast<Literal*>(expr);
    if (literalPtr) {
        return literalPtr->value;
    }
     throw runtime_error("invalid");
}
// SELECRT * FROM EMPLOYEES where AGE > 12
//         where (AGE>12) OR(AGE<13)
bool applyFilter(Expression *expr, const Employee &emp) {
    // expr -> should be binaryexpreesion static_cast dyanmic cast reinterpret cast

    auto *ptr = dynamic_cast<BinaryExpression *>(expr);
    if (!ptr) throw runtime_error("expression not valid");

    if (ptr->op == "OR") {
        return applyFilter(ptr->left, emp) || applyFilter(ptr->right, emp);
    }
    //     >
    // AGE    NUMBER EXPREESION
    int left =getInt(ptr->left, emp);
    int right = getInt(ptr->right, emp);
    if (ptr->op == ">")  return left > right;
    if (ptr->op == "<")  return left < right;
    throw runtime_error("INVALID");
}

// ------------------- Executor -------------------
void execute(SelectStatement &stmt, vector<Employee> &employees) {
    for (const auto &emp: employees) {
        if (applyFilter(stmt.whereFilter, emp)) {
            // apply project
            if (stmt.column == "name") {
                std::cout << emp.name << std::endl;
            }
        }
    }
}

// ------------------- Main -------------------
int main() {
    vector<Employee> employees = {
        {"Kartik", 1, 20},
        {"Krishank", 2, 30},{"Sandip", 3, 15},
        {"Nitish", 4, 17},{"Kp", 5, 20}
    };
    string sqlQuery = "SELECT name FROM employees WHERE (age<18 OR id<2)";
    Lexer lexer(sqlQuery);
    auto tokens = lexer.tokenize();

    DbParser parser(tokens);
    SelectStatement stmt = parser.parseSelect();

    execute(stmt, employees);

    return 0;
}

