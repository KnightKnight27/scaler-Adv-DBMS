#include <cctype>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

enum class TokenKind {
    Select,
    From,
    Where,
    And,
    Or,
    Identifier,
    Number,
    Star,
    Comma,
    Gt,
    Lt,
    Eq,
    Ne,
    Gte,
    Lte,
    LParen,
    RParen,
    End,
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
};

std::string upper_copy(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return text;
}

std::vector<Token> lex(const std::string& input) {
    std::vector<Token> tokens;

    for (std::size_t i = 0; i < input.size();) {
        const unsigned char ch = static_cast<unsigned char>(input[i]);
        if (std::isspace(ch)) {
            ++i;
            continue;
        }

        if (std::isalpha(ch) || input[i] == '_') {
            std::size_t start = i++;
            while (i < input.size()) {
                const unsigned char next = static_cast<unsigned char>(input[i]);
                if (!std::isalnum(next) && input[i] != '_') {
                    break;
                }
                ++i;
            }
            std::string word = input.substr(start, i - start);
            std::string upper = upper_copy(word);
            if (upper == "SELECT") tokens.push_back({TokenKind::Select, word});
            else if (upper == "FROM") tokens.push_back({TokenKind::From, word});
            else if (upper == "WHERE") tokens.push_back({TokenKind::Where, word});
            else if (upper == "AND") tokens.push_back({TokenKind::And, word});
            else if (upper == "OR") tokens.push_back({TokenKind::Or, word});
            else tokens.push_back({TokenKind::Identifier, word});
            continue;
        }

        if (std::isdigit(ch)) {
            std::size_t start = i++;
            while (i < input.size() && std::isdigit(static_cast<unsigned char>(input[i]))) {
                ++i;
            }
            tokens.push_back({TokenKind::Number, input.substr(start, i - start)});
            continue;
        }

        if (i + 1 < input.size()) {
            const std::string two = input.substr(i, 2);
            if (two == ">=") {
                tokens.push_back({TokenKind::Gte, two});
                i += 2;
                continue;
            }
            if (two == "<=") {
                tokens.push_back({TokenKind::Lte, two});
                i += 2;
                continue;
            }
            if (two == "!=") {
                tokens.push_back({TokenKind::Ne, two});
                i += 2;
                continue;
            }
        }

        switch (input[i]) {
            case '*': tokens.push_back({TokenKind::Star, "*"}); break;
            case ',': tokens.push_back({TokenKind::Comma, ","}); break;
            case '>': tokens.push_back({TokenKind::Gt, ">"}); break;
            case '<': tokens.push_back({TokenKind::Lt, "<"}); break;
            case '=': tokens.push_back({TokenKind::Eq, "="}); break;
            case '(': tokens.push_back({TokenKind::LParen, "("}); break;
            case ')': tokens.push_back({TokenKind::RParen, ")"}); break;
            default:
                throw std::runtime_error(std::string("unexpected SQL character: ") + input[i]);
        }
        ++i;
    }

    tokens.push_back({TokenKind::End, ""});
    return tokens;
}

struct Expr {
    std::string op;
    std::string column;
    int literal = 0;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

struct SelectQuery {
    std::vector<std::string> columns;
    std::string table;
    std::unique_ptr<Expr> where;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    SelectQuery parse_select() {
        consume(TokenKind::Select);

        SelectQuery query;
        if (match(TokenKind::Star)) {
            query.columns.clear();
        } else {
            query.columns.push_back(consume(TokenKind::Identifier).text);
            while (match(TokenKind::Comma)) {
                query.columns.push_back(consume(TokenKind::Identifier).text);
            }
        }

        consume(TokenKind::From);
        query.table = consume(TokenKind::Identifier).text;

        if (match(TokenKind::Where)) {
            query.where = parse_or();
        }
        consume(TokenKind::End);
        return query;
    }

private:
    std::unique_ptr<Expr> parse_or() {
        auto left = parse_and();
        while (match(TokenKind::Or)) {
            auto node = std::make_unique<Expr>();
            node->op = "OR";
            node->left = std::move(left);
            node->right = parse_and();
            left = std::move(node);
        }
        return left;
    }

    std::unique_ptr<Expr> parse_and() {
        auto left = parse_primary();
        while (match(TokenKind::And)) {
            auto node = std::make_unique<Expr>();
            node->op = "AND";
            node->left = std::move(left);
            node->right = parse_primary();
            left = std::move(node);
        }
        return left;
    }

    std::unique_ptr<Expr> parse_primary() {
        if (match(TokenKind::LParen)) {
            auto expr = parse_or();
            consume(TokenKind::RParen);
            return expr;
        }
        return parse_comparison();
    }

    std::unique_ptr<Expr> parse_comparison() {
        auto node = std::make_unique<Expr>();
        node->column = consume(TokenKind::Identifier).text;

        if (match(TokenKind::Gt)) node->op = ">";
        else if (match(TokenKind::Lt)) node->op = "<";
        else if (match(TokenKind::Eq)) node->op = "=";
        else if (match(TokenKind::Ne)) node->op = "!=";
        else if (match(TokenKind::Gte)) node->op = ">=";
        else if (match(TokenKind::Lte)) node->op = "<=";
        else throw std::runtime_error("expected comparison operator after " + node->column);

        node->literal = std::stoi(consume(TokenKind::Number).text);
        return node;
    }

    bool match(TokenKind kind) {
        if (tokens_[pos_].kind != kind) {
            return false;
        }
        ++pos_;
        return true;
    }

    Token consume(TokenKind kind) {
        if (tokens_[pos_].kind != kind) {
            throw std::runtime_error("unexpected token: " + tokens_[pos_].text);
        }
        return tokens_[pos_++];
    }

    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
};

struct Row {
    int id = 0;
    std::string name;
    int age = 0;
    int marks = 0;
};

int numeric_column(const Row& row, const std::string& column) {
    if (column == "id") return row.id;
    if (column == "age") return row.age;
    if (column == "marks") return row.marks;
    throw std::runtime_error("unknown numeric column: " + column);
}

bool evaluate(const Expr* expr, const Row& row) {
    if (expr == nullptr) {
        return true;
    }
    if (expr->op == "AND") {
        return evaluate(expr->left.get(), row) && evaluate(expr->right.get(), row);
    }
    if (expr->op == "OR") {
        return evaluate(expr->left.get(), row) || evaluate(expr->right.get(), row);
    }

    const int lhs = numeric_column(row, expr->column);
    const int rhs = expr->literal;
    if (expr->op == ">") return lhs > rhs;
    if (expr->op == "<") return lhs < rhs;
    if (expr->op == "=") return lhs == rhs;
    if (expr->op == "!=") return lhs != rhs;
    if (expr->op == ">=") return lhs >= rhs;
    if (expr->op == "<=") return lhs <= rhs;
    throw std::runtime_error("unknown operator: " + expr->op);
}

std::vector<std::string> output_columns(const SelectQuery& query) {
    if (!query.columns.empty()) {
        return query.columns;
    }
    return {"id", "name", "age", "marks"};
}

void print_cell(const Row& row, const std::string& column) {
    if (column == "id") std::cout << row.id;
    else if (column == "name") std::cout << row.name;
    else if (column == "age") std::cout << row.age;
    else if (column == "marks") std::cout << row.marks;
    else throw std::runtime_error("unknown output column: " + column);
}

void execute(const SelectQuery& query, const std::vector<Row>& rows) {
    const std::vector<std::string> columns = output_columns(query);
    std::cout << "table: " << query.table << '\n';
    for (const Row& row : rows) {
        if (!evaluate(query.where.get(), row)) {
            continue;
        }
        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (i != 0) {
                std::cout << " | ";
            }
            std::cout << columns[i] << '=';
            print_cell(row, columns[i]);
        }
        std::cout << '\n';
    }
}

int main() {
    const std::vector<Row> students = {
        {1, "Asha", 22, 91},
        {2, "Kabir", 24, 76},
        {3, "Meera", 21, 88},
        {4, "Rohan", 25, 82},
        {5, "Diya", 23, 93},
    };

    const std::string sql =
        "SELECT name, marks FROM students WHERE marks >= 80 AND (age < 23 OR id > 4)";

    std::cout << "sql: " << sql << "\n\n";
    Parser parser(lex(sql));
    const SelectQuery query = parser.parse_select();
    execute(query, students);

    return 0;
}
