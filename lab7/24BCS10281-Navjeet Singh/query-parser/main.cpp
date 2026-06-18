#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>

using namespace std;


struct Employee {
    string employeeName;
    int employeeId;
    int employeeAge;

    int getNumericValue(const string& column) const {
        if (column == "id")  return employeeId;
        if (column == "age") return employeeAge;

        throw runtime_error("Unknown numeric column: " + column);
    }

    string getTextValue(const string& column) const {
        if (column == "name") return employeeName;

        throw runtime_error("Unknown text column: " + column);
    }
};


enum class TokenType {
    SELECT_KW,
    FROM_KW,
    WHERE_KW,
    OR_KW,

    IDENTIFIER,
    NUMBER,

    GREATER,
    LESSER,
    EQUAL,
    GREATERTHANEQUAL,
    LESSERTHANEQUAL,

    LEFT_BRACKET,
    RIGHT_BRACKET,

    END_OF_INPUT
};

struct Token {
    TokenType type;
    string value;
};


class Lexer {
private:
    string queryText;

public:
    Lexer(const string& text) : queryText(text) {}

    vector<Token> tokenize() {
        vector<Token> tokens;
        int pos = 0;

        while (pos < queryText.size()) {

            if (isspace(queryText[pos])) {
                pos++;
                continue;
            }

            if (isalpha(queryText[pos])) {

                string word;

                while (pos < queryText.size() &&
                       (isalnum(queryText[pos]) ||
                        queryText[pos] == '_')) {

                    word += queryText[pos++];
                }

                string upperWord = word;
                transform(
                    upperWord.begin(),
                    upperWord.end(),
                    upperWord.begin(),
                    ::toupper
                );

                if (upperWord == "SELECT")
                    tokens.push_back({TokenType::SELECT_KW, word});
                else if (upperWord == "FROM")
                    tokens.push_back({TokenType::FROM_KW, word});
                else if (upperWord == "WHERE")
                    tokens.push_back({TokenType::WHERE_KW, word});
                else if (upperWord == "OR")
                    tokens.push_back({TokenType::OR_KW, word});
                else
                    tokens.push_back({TokenType::IDENTIFIER, word});

                continue;
            }

            if (isdigit(queryText[pos])) {

                string digits;

                while (pos < queryText.size() &&
                       isdigit(queryText[pos])) {

                    digits += queryText[pos++];
                }

                tokens.push_back({TokenType::NUMBER, digits});
                continue;
            }

            if (queryText[pos] == '>') {

                if (pos + 1 < queryText.size() &&
                    queryText[pos + 1] == '=') {

                    tokens.push_back({TokenType::GREATERTHANEQUAL, ">="});
                    pos += 2;
                }
                else {
                    tokens.push_back({TokenType::GREATER, ">"});
                    pos++;
                }

                continue;
            }

            if (queryText[pos] == '<') {

                if (pos + 1 < queryText.size() &&
                    queryText[pos + 1] == '=') {

                    tokens.push_back({TokenType::LESSERTHANEQUAL, "<="});
                    pos += 2;
                }
                else {
                    tokens.push_back({TokenType::LESSER, "<"});
                    pos++;
                }

                continue;
            }

            if (queryText[pos] == '=') {
                tokens.push_back({TokenType::EQUAL, "="});
                pos++;
                continue;
            }

            if (queryText[pos] == '(') {
                tokens.push_back({TokenType::LEFT_BRACKET, "("});
                pos++;
                continue;
            }

            if (queryText[pos] == ')') {
                tokens.push_back({TokenType::RIGHT_BRACKET, ")"});
                pos++;
                continue;
            }

            pos++;
        }

        tokens.push_back({TokenType::END_OF_INPUT, ""});
        return tokens;
    }
};


struct Expression {
    virtual ~Expression() = default;
};

struct IntegerLiteral : Expression {
    int value;

    IntegerLiteral(int v)
        : value(v) {}
};

struct ColumnReference : Expression {
    string column;

    ColumnReference(const string& c)
        : column(c) {}
};

struct OperationNode : Expression {
    string operation;
    Expression* left;
    Expression* right;

    OperationNode(
        const string& op,
        Expression* lhs,
        Expression* rhs
    )
        : operation(op),
          left(lhs),
          right(rhs) {}
};



struct SelectStatement {
    string selectedColumn;
    string tableName;
    Expression* filterCondition;
};



class Parser {
private:
    vector<Token> tokens;
    int current = 0;

    Token consume(TokenType expected) {
        if (tokens[current].type != expected)
            throw runtime_error("Unexpected token");

        return tokens[current++];
    }

    Expression* parseComparison() {

        string column =
            consume(TokenType::IDENTIFIER).value;

        Expression* left =
            new ColumnReference(column);

        string op;

        if (tokens[current].type == TokenType::GREATERTHANEQUAL) {
            op = ">=";
            consume(TokenType::GREATERTHANEQUAL);
        }
        else if (tokens[current].type == TokenType::LESSERTHANEQUAL) {
            op = "<=";
            consume(TokenType::LESSERTHANEQUAL);
        }
        else if (tokens[current].type == TokenType::GREATER) {
            op = ">";
            consume(TokenType::GREATER);
        }
        else if (tokens[current].type == TokenType::LESSER) {
            op = "<";
            consume(TokenType::LESSER);
        }
        else {
            throw runtime_error(
                "Comparison operator expected"
            );
        }

        int value =
            stoi(consume(TokenType::NUMBER).value);

        Expression* right =
            new IntegerLiteral(value);

        return new OperationNode(op, left, right);
    }

    Expression* parsePrimary() {

        if (tokens[current].type ==
            TokenType::LEFT_BRACKET) {

            consume(TokenType::LEFT_BRACKET);

            Expression* inner =
                parseOrExpression();

            consume(TokenType::RIGHT_BRACKET);

            return inner;
        }

        return parseComparison();
    }

    Expression* parseOrExpression() {

        Expression* left =
            parsePrimary();

        while (tokens[current].type ==
               TokenType::OR_KW) {

            consume(TokenType::OR_KW);

            Expression* right =
                parsePrimary();

            left = new OperationNode(
                "OR",
                left,
                right
            );
        }

        return left;
    }

public:
    Parser(const vector<Token>& t)
        : tokens(t) {}

    SelectStatement parse() {

        consume(TokenType::SELECT_KW);

        string selected =
            consume(TokenType::IDENTIFIER).value;

        consume(TokenType::FROM_KW);

        string table =
            consume(TokenType::IDENTIFIER).value;

        consume(TokenType::WHERE_KW);

        Expression* condition =
            parseOrExpression();

        return {
            selected,
            table,
            condition
        };
    }
};



int evaluateValue(
    Expression* expr,
    const Employee& employee
) {

    if (auto* column =
            dynamic_cast<ColumnReference*>(expr)) {

        return employee.getNumericValue(
            column->column
        );
    }

    if (auto* literal =
            dynamic_cast<IntegerLiteral*>(expr)) {

        return literal->value;
    }

    throw runtime_error(
        "Invalid numeric expression"
    );
}

bool evaluateCondition(
    Expression* expr,
    const Employee& employee
) {

    auto* operation =
        dynamic_cast<OperationNode*>(expr);

    if (!operation)
        throw runtime_error(
            "Invalid condition"
        );

    if (operation->operation == "OR") {

        return evaluateCondition(
                   operation->left,
                   employee
               ) ||
               evaluateCondition(
                   operation->right,
                   employee
               );
    }

    int lhs =
        evaluateValue(
            operation->left,
            employee
        );

    int rhs =
        evaluateValue(
            operation->right,
            employee
        );

    if (operation->operation == ">")
        return lhs > rhs;

    if (operation->operation == "<")
        return lhs < rhs;

    if (operation->operation == ">=")
        return lhs >= rhs;

    if (operation->operation == "<=")
        return lhs <= rhs;

    if (operation->operation == "=")
        return lhs == rhs;

    throw runtime_error(
        "Unknown operator"
    );
}


void executeQuery(
    const SelectStatement& query,
    const vector<Employee>& employees
) {

    for (const auto& employee : employees) {

        if (!evaluateCondition(
                query.filterCondition,
                employee))
            continue;

        if (query.selectedColumn == "name")
            cout
                << employee.getTextValue("name")
                << endl;

        else if (query.selectedColumn == "id")
            cout
                << employee.getNumericValue("id")
                << endl;

        else if (query.selectedColumn == "age")
            cout
                << employee.getNumericValue("age")
                << endl;
    }
}

int main() {

    vector<Employee> employees = {
        {"Aarav", 1, 22},
        {"Diya", 2, 22},
        {"Rohan", 3, 28},
        {"Meera", 4, 24},
        {"Kabir", 5, 22}
    };

    string query =
        "SELECT name FROM employees "
        "WHERE id <= 3";

    Lexer lexer(query);

    vector<Token> tokens =
        lexer.tokenize();

    Parser parser(tokens);

    SelectStatement statement =
        parser.parse();

    executeQuery(
        statement,
        employees
    );

    return 0;
}
