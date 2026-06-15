#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

enum class TokenType
{
    Keyword,
    Identifier,
    Number,
    String,
    Operator,
    Comma,
    Star,
    LeftParen,
    RightParen,
    Semicolon,
    End
};

struct Token
{
    TokenType type;
    std::string text;
};

struct Row
{
    std::map<std::string, std::string> values;
};

struct Query
{
    std::vector<std::string> columns;
    std::string table;
};

enum class ExprKind
{
    Comparison,
    And,
    Or
};

struct Expression
{
    ExprKind kind;
    std::string column;
    std::string op;
    std::string literal;
    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> right;
};

std::string toUpper(std::string value)
{
    for (char& ch : value)
    {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }

    return value;
}

bool isKeyword(const std::string& value)
{
    const std::string word = toUpper(value);
    return word == "SELECT" || word == "FROM" || word == "WHERE" || word == "AND" || word == "OR";
}

bool isIntegerText(const std::string& value)
{
    if (value.empty())
    {
        return false;
    }

    std::size_t start = value[0] == '-' ? 1 : 0;
    if (start == value.size())
    {
        return false;
    }

    for (std::size_t i = start; i < value.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(value[i])))
        {
            return false;
        }
    }

    return true;
}

std::vector<Token> tokenize(const std::string& query)
{
    std::vector<Token> tokens;

    for (std::size_t i = 0; i < query.size();)
    {
        const char ch = query[i];

        if (std::isspace(static_cast<unsigned char>(ch)))
        {
            ++i;
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_')
        {
            std::string word;
            while (i < query.size()
                && (std::isalnum(static_cast<unsigned char>(query[i])) || query[i] == '_'))
            {
                word.push_back(query[i]);
                ++i;
            }

            tokens.push_back({isKeyword(word) ? TokenType::Keyword : TokenType::Identifier, word});
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(ch)))
        {
            std::string number;
            while (i < query.size() && std::isdigit(static_cast<unsigned char>(query[i])))
            {
                number.push_back(query[i]);
                ++i;
            }

            tokens.push_back({TokenType::Number, number});
            continue;
        }

        if (ch == '\'')
        {
            ++i;
            std::string value;
            while (i < query.size() && query[i] != '\'')
            {
                value.push_back(query[i]);
                ++i;
            }

            if (i == query.size())
            {
                throw std::runtime_error("Unclosed string literal");
            }

            ++i;
            tokens.push_back({TokenType::String, value});
            continue;
        }

        if (ch == ',' || ch == '*' || ch == '(' || ch == ')' || ch == ';')
        {
            TokenType type = TokenType::Comma;
            if (ch == '*')
            {
                type = TokenType::Star;
            }
            else if (ch == '(')
            {
                type = TokenType::LeftParen;
            }
            else if (ch == ')')
            {
                type = TokenType::RightParen;
            }
            else if (ch == ';')
            {
                type = TokenType::Semicolon;
            }

            tokens.push_back({type, std::string(1, ch)});
            ++i;
            continue;
        }

        if (ch == '=' || ch == '!' || ch == '<' || ch == '>')
        {
            std::string op(1, ch);
            if (i + 1 < query.size() && query[i + 1] == '=')
            {
                op.push_back('=');
                ++i;
            }

            if (op == "!")
            {
                throw std::runtime_error("Unsupported operator '!'");
            }

            tokens.push_back({TokenType::Operator, op});
            ++i;
            continue;
        }

        throw std::runtime_error(std::string("Unexpected character: ") + ch);
    }

    tokens.push_back({TokenType::End, ""});
    return tokens;
}

class Parser
{
public:
    explicit Parser(std::vector<Token> input)
        : tokens(std::move(input))
    {
    }

    std::pair<Query, std::unique_ptr<Expression>> parse()
    {
        Query query;
        consumeKeyword("SELECT");
        query.columns = parseColumnList();
        consumeKeyword("FROM");
        query.table = consume(TokenType::Identifier, "Expected table name").text;

        std::unique_ptr<Expression> condition;
        if (matchKeyword("WHERE"))
        {
            condition = parseOr();
        }

        if (current().type == TokenType::Semicolon)
        {
            advance();
        }

        consume(TokenType::End, "Unexpected token after query");
        return {query, std::move(condition)};
    }

private:
    std::vector<Token> tokens;
    std::size_t position{0};

    const Token& current() const
    {
        return tokens[position];
    }

    const Token& advance()
    {
        return tokens[position++];
    }

    bool match(TokenType type)
    {
        if (current().type == type)
        {
            advance();
            return true;
        }

        return false;
    }

    bool matchKeyword(const std::string& word)
    {
        if (current().type == TokenType::Keyword && toUpper(current().text) == word)
        {
            advance();
            return true;
        }

        return false;
    }

    Token consume(TokenType type, const std::string& message)
    {
        if (current().type != type)
        {
            throw std::runtime_error(message + ", got '" + current().text + "'");
        }

        return advance();
    }

    void consumeKeyword(const std::string& word)
    {
        if (!matchKeyword(word))
        {
            throw std::runtime_error("Expected keyword " + word);
        }
    }

    std::vector<std::string> parseColumnList()
    {
        std::vector<std::string> columns;

        if (match(TokenType::Star))
        {
            columns.push_back("*");
            return columns;
        }

        columns.push_back(consume(TokenType::Identifier, "Expected column name").text);
        while (match(TokenType::Comma))
        {
            columns.push_back(consume(TokenType::Identifier, "Expected column name after comma").text);
        }

        return columns;
    }

    std::unique_ptr<Expression> parseOr()
    {
        auto node = parseAnd();

        while (matchKeyword("OR"))
        {
            auto parent = std::make_unique<Expression>();
            parent->kind = ExprKind::Or;
            parent->left = std::move(node);
            parent->right = parseAnd();
            node = std::move(parent);
        }

        return node;
    }

    std::unique_ptr<Expression> parseAnd()
    {
        auto node = parsePrimary();

        while (matchKeyword("AND"))
        {
            auto parent = std::make_unique<Expression>();
            parent->kind = ExprKind::And;
            parent->left = std::move(node);
            parent->right = parsePrimary();
            node = std::move(parent);
        }

        return node;
    }

    std::unique_ptr<Expression> parsePrimary()
    {
        if (match(TokenType::LeftParen))
        {
            auto nested = parseOr();
            consume(TokenType::RightParen, "Expected closing parenthesis");
            return nested;
        }

        return parseComparison();
    }

    std::unique_ptr<Expression> parseComparison()
    {
        auto node = std::make_unique<Expression>();
        node->kind = ExprKind::Comparison;
        node->column = consume(TokenType::Identifier, "Expected column in condition").text;
        node->op = consume(TokenType::Operator, "Expected comparison operator").text;

        if (current().type == TokenType::Number || current().type == TokenType::String || current().type == TokenType::Identifier)
        {
            node->literal = advance().text;
            return node;
        }

        throw std::runtime_error("Expected literal value in comparison");
    }
};

std::string readValue(const Row& row, const std::string& column)
{
    auto it = row.values.find(column);
    if (it == row.values.end())
    {
        throw std::runtime_error("Unknown column: " + column);
    }

    return it->second;
}

bool compareValues(const std::string& left, const std::string& op, const std::string& right)
{
    if (isIntegerText(left) && isIntegerText(right))
    {
        const int a = std::stoi(left);
        const int b = std::stoi(right);

        if (op == "=")
        {
            return a == b;
        }
        if (op == "!=")
        {
            return a != b;
        }
        if (op == "<")
        {
            return a < b;
        }
        if (op == "<=")
        {
            return a <= b;
        }
        if (op == ">")
        {
            return a > b;
        }
        if (op == ">=")
        {
            return a >= b;
        }
    }

    if (op == "=")
    {
        return left == right;
    }
    if (op == "!=")
    {
        return left != right;
    }
    if (op == "<")
    {
        return left < right;
    }
    if (op == "<=")
    {
        return left <= right;
    }
    if (op == ">")
    {
        return left > right;
    }
    if (op == ">=")
    {
        return left >= right;
    }

    throw std::runtime_error("Unsupported comparison: " + op);
}

bool evaluate(const Expression* expression, const Row& row)
{
    if (expression == nullptr)
    {
        return true;
    }

    if (expression->kind == ExprKind::And)
    {
        return evaluate(expression->left.get(), row) && evaluate(expression->right.get(), row);
    }

    if (expression->kind == ExprKind::Or)
    {
        return evaluate(expression->left.get(), row) || evaluate(expression->right.get(), row);
    }

    return compareValues(readValue(row, expression->column), expression->op, expression->literal);
}

std::vector<Row> sampleEmployees()
{
    return {
        {{{"id", "1"}, {"name", "Aarav"}, {"department", "Engineering"}, {"age", "31"}, {"salary", "82000"}}},
        {{{"id", "2"}, {"name", "Meera"}, {"department", "HR"}, {"age", "28"}, {"salary", "61000"}}},
        {{{"id", "3"}, {"name", "Kabir"}, {"department", "Engineering"}, {"age", "24"}, {"salary", "68000"}}},
        {{{"id", "4"}, {"name", "Ishaan"}, {"department", "Finance"}, {"age", "35"}, {"salary", "90000"}}},
        {{{"id", "5"}, {"name", "Naina"}, {"department", "Engineering"}, {"age", "39"}, {"salary", "105000"}}}
    };
}

std::vector<std::string> expandColumns(const Query& query)
{
    if (query.columns.size() == 1 && query.columns.front() == "*")
    {
        return {"id", "name", "department", "age", "salary"};
    }

    return query.columns;
}

void runQuery(const std::string& sql, const std::vector<Row>& rows)
{
    std::cout << "Query: " << sql << '\n';

    Parser parser(tokenize(sql));
    auto [query, condition] = parser.parse();

    if (query.table != "employees")
    {
        throw std::runtime_error("Only the employees table is available in this demo");
    }

    const std::vector<std::string> columns = expandColumns(query);
    int matched = 0;

    for (const Row& row : rows)
    {
        if (!evaluate(condition.get(), row))
        {
            continue;
        }

        ++matched;
        for (const std::string& column : columns)
        {
            std::cout << column << '=' << readValue(row, column) << ' ';
        }
        std::cout << '\n';
    }

    std::cout << "Rows matched: " << matched << "\n\n";
}

int main()
{
    const std::vector<Row> employees = sampleEmployees();
    const std::vector<std::string> queries{
        "SELECT name, department FROM employees WHERE age >= 30 AND salary > 70000;",
        "SELECT * FROM employees WHERE department = 'Engineering' OR age < 26;",
        "SELECT id, name FROM employees WHERE (salary >= 80000 AND age < 40) OR department = 'HR';"
    };

    try
    {
        for (const std::string& query : queries)
        {
            runQuery(query, employees);
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
