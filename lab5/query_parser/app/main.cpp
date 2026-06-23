#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>

#include "types.h"
#include "lexer.h"
#include "expressions.h"
#include "select_stat.h"
#include "row.h"

using namespace std;

// ------------------- Parser -------------------

class DbParser {
public:
    explicit DbParser(vector<Token> toks)
        : tokens(move(toks)) {}

    SelectStatement parseSelect() {
        consume(TokenType::SELECT);

        string column =
            consume(TokenType::IDENTIFIER).text;

        consume(TokenType::FROM);

        string table =
            consume(TokenType::IDENTIFIER).text;

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

    Token &current() {
        return tokens[pos];
    }

    Token consume(TokenType expected) {
        if (current().type != expected) {
            throw runtime_error("Unexpected token");
        }
        return tokens[pos++];
    }

    Expression *parseExpression() {
        auto left = parsePrimary();

        while (current().type == TokenType::OR) {
            consume(TokenType::OR);

            auto right = parsePrimary();

            left = new BinaryExpression(
                "OR",
                left,
                right
            );
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
        string column =
            consume(TokenType::IDENTIFIER).text;

        Expression *left =
            new ColumnRef(column);

        string op;

        if (current().type == TokenType::GT) {
            op = ">";
            consume(TokenType::GT);
        }
        else if (current().type == TokenType::LT) {
            op = "<";
            consume(TokenType::LT);
        }
        else {
            throw runtime_error(
                "Expected > or <"
            );
        }

        int value =
            stoi(
                consume(TokenType::NUMBER).text
            );

        Expression *right =
            new Literal(value);

        return new BinaryExpression(
            op,
            left,
            right
        );
    }
};

// ------------------- Evaluator -------------------

int getInt(Expression *expr,
           const Row &row) {

    if (auto *col =
            dynamic_cast<ColumnRef *>(expr)) {

        return get<int>(
            row.columns.at(col->name)
        );
    }

    if (auto *lit =
            dynamic_cast<Literal *>(expr)) {

        return lit->value;
    }

    throw runtime_error(
        "Invalid integer expression"
    );
}

bool evaluate(Expression *expr,
              const Row &row) {

    auto *bin =
        dynamic_cast<BinaryExpression *>(expr);

    if (!bin) {
        throw runtime_error(
            "Invalid expression"
        );
    }

    if (bin->op == "OR") {
        return evaluate(bin->left, row) ||
               evaluate(bin->right, row);
    }

    int left =
        getInt(bin->left, row);

    int right =
        getInt(bin->right, row);

    if (bin->op == ">")
        return left > right;

    if (bin->op == "<")
        return left < right;

    throw runtime_error(
        "Unknown operator"
    );
}

// ------------------- Executor -------------------

void execute(
    SelectStatement &stmt,
    const vector<Row> &rows) {

    for (const auto &row : rows) {

        if (!evaluate(
                stmt.whereFilter,
                row))
            continue;

        const auto &value =
            row.columns.at(stmt.column);

        if (holds_alternative<int>(value)) {
            cout << get<int>(value);
        }

        if (holds_alternative<string>(value)) {
            cout << get<string>(value);
        }

        cout << '\n';
    }
}

// ------------------- Main -------------------

int main() {

    vector<Row> rows = {
        {
            {
                {"name", string("Kartik")},
                {"id", 1},
                {"age", 20}
            }
        },
        {
            {
                {"name", string("Krishank")},
                {"id", 2},
                {"age", 30}
            }
        },
        {
            {
                {"name", string("Sandip")},
                {"id", 3},
                {"age", 15}
            }
        },
        {
            {
                {"name", string("Nitish")},
                {"id", 4},
                {"age", 17}
            }
        },
        {
            {
                {"name", string("KP")},
                {"id", 5},
                {"age", 20}
            }
        }
    };

    string sql =
        "SELECT name FROM employees "
        "WHERE (age < 18 OR id < 2)";

    Lexer lexer(sql);

    auto tokens =
        lexer.tokenize();

    DbParser parser(tokens);

    SelectStatement stmt =
        parser.parseSelect();

    execute(stmt, rows);

    return 0;
}