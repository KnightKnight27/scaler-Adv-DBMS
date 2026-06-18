// Lab 7 - SQL SELECT parser (recursive descent → AST → evaluator)
// Pranav (24BCS10237) <pranav.24bcs10237@sst.scaler.com>
//
// Parses queries of the shape:
//   SELECT <col> FROM <table> WHERE <expr>
// where <expr> is a boolean combination of comparisons:
//   <expr>      := <orTerm>
//   <orTerm>    := <andTerm> ( OR  <andTerm> )*
//   <andTerm>   := <factor>  ( AND <factor>  )*
//   <factor>    := '(' <expr> ')' | <comparison>
//   <comparison>:= <ident> <op> <number>
//   <op>        := > | < | >= | <= | = | !=
// AST nodes are owned via std::unique_ptr.

#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct Employee {
    std::string name;
    int id  = 0;
    int age = 0;
};

struct Node {
    std::string op;                       // ">", "<", "AND", "OR", ...
    std::string col;                      // populated for comparison leaves
    int         val = 0;
    std::unique_ptr<Node> l;
    std::unique_ptr<Node> r;
};

namespace {

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

}  // namespace

std::vector<std::string> tokenize(const std::string& q) {
    std::vector<std::string> tokens;
    size_t i = 0;
    const size_t n = q.size();

    while (i < n) {
        unsigned char c = static_cast<unsigned char>(q[i]);
        if (std::isspace(c)) { ++i; continue; }

        if (std::isalpha(c) || c == '_') {
            std::string lex;
            while (i < n && (std::isalnum(static_cast<unsigned char>(q[i])) || q[i] == '_'))
                lex += q[i++];
            tokens.push_back(std::move(lex));
        } else if (std::isdigit(c)) {
            std::string lex;
            while (i < n && std::isdigit(static_cast<unsigned char>(q[i])))
                lex += q[i++];
            tokens.push_back(std::move(lex));
        } else if ((q[i] == '>' || q[i] == '<' || q[i] == '!') && i + 1 < n && q[i + 1] == '=') {
            tokens.push_back({q[i], q[i + 1]});
            i += 2;
        } else {
            tokens.push_back(std::string(1, q[i]));
            ++i;
        }
    }
    return tokens;
}

struct ParsedQuery {
    std::string selectedColumn;
    std::string tableName;
    std::unique_ptr<Node> whereRoot;
};

class Parser {
public:
    explicit Parser(std::vector<std::string> toks) : tokens_(std::move(toks)) {}

    ParsedQuery parseQuery() {
        expectKeyword("SELECT");
        std::string col = consume();
        expectKeyword("FROM");
        std::string table = consume();
        expectKeyword("WHERE");
        auto where = parseOr();
        if (cursor_ != tokens_.size())
            throw std::runtime_error("Unexpected token after WHERE clause: '" + tokens_[cursor_] + "'");
        return {std::move(col), std::move(table), std::move(where)};
    }

private:
    std::vector<std::string> tokens_;
    size_t cursor_ = 0;

    const std::string& peek() const {
        if (cursor_ >= tokens_.size()) throw std::runtime_error("Unexpected end of input");
        return tokens_[cursor_];
    }

    std::string consume() {
        if (cursor_ >= tokens_.size()) throw std::runtime_error("Unexpected end of input");
        return tokens_[cursor_++];
    }

    void expectKeyword(const std::string& kw) {
        std::string got = toUpper(consume());
        if (got != kw) throw std::runtime_error("Expected " + kw + " keyword, got '" + got + "'");
    }

    bool peekIsKeyword(const std::string& kw) const {
        return cursor_ < tokens_.size() && toUpper(tokens_[cursor_]) == kw;
    }

    std::unique_ptr<Node> parseOr() {
        auto lhs = parseAnd();
        while (peekIsKeyword("OR")) {
            ++cursor_;
            auto rhs = parseAnd();
            auto node = std::make_unique<Node>();
            node->op = "OR";
            node->l  = std::move(lhs);
            node->r  = std::move(rhs);
            lhs = std::move(node);
        }
        return lhs;
    }

    std::unique_ptr<Node> parseAnd() {
        auto lhs = parseFactor();
        while (peekIsKeyword("AND")) {
            ++cursor_;
            auto rhs = parseFactor();
            auto node = std::make_unique<Node>();
            node->op = "AND";
            node->l  = std::move(lhs);
            node->r  = std::move(rhs);
            lhs = std::move(node);
        }
        return lhs;
    }

    std::unique_ptr<Node> parseFactor() {
        if (peek() == "(") {
            ++cursor_;
            auto inner = parseOr();
            if (cursor_ >= tokens_.size() || tokens_[cursor_] != ")")
                throw std::runtime_error("Mismatched parenthesis: expected ')'");
            ++cursor_;
            return inner;
        }
        return parseComparison();
    }

    std::unique_ptr<Node> parseComparison() {
        std::string col = consume();
        std::string op  = consume();
        std::string num = consume();

        if (op != ">" && op != "<" && op != ">=" && op != "<=" && op != "=" && op != "!=")
            throw std::runtime_error("Unsupported comparison operator: '" + op + "'");

        auto node = std::make_unique<Node>();
        node->op  = op;
        node->col = col;
        node->val = std::stoi(num);
        return node;
    }
};

bool evaluate(const Node* node, const Employee& e) {
    if (!node) return false;

    if (node->op == "AND") return evaluate(node->l.get(), e) && evaluate(node->r.get(), e);
    if (node->op == "OR")  return evaluate(node->l.get(), e) || evaluate(node->r.get(), e);

    int colValue = 0;
    if      (node->col == "id")  colValue = e.id;
    else if (node->col == "age") colValue = e.age;
    else throw std::runtime_error("Unknown column in condition: '" + node->col + "'");

    const auto& op = node->op;
    if (op == ">")  return colValue >  node->val;
    if (op == "<")  return colValue <  node->val;
    if (op == ">=") return colValue >= node->val;
    if (op == "<=") return colValue <= node->val;
    if (op == "=")  return colValue == node->val;
    if (op == "!=") return colValue != node->val;
    throw std::runtime_error("Unsupported comparison operator: '" + op + "'");
}

void runQuery(const std::string& sql, const std::vector<Employee>& employees) {
    auto tokens = tokenize(sql);
    Parser parser(std::move(tokens));
    auto query = parser.parseQuery();

    std::cout << "\nQuery: " << sql << '\n';
    for (const auto& e : employees) {
        if (!evaluate(query.whereRoot.get(), e)) continue;
        if      (query.selectedColumn == "name") std::cout << "  " << e.name << '\n';
        else if (query.selectedColumn == "id")   std::cout << "  " << e.id   << '\n';
        else if (query.selectedColumn == "age")  std::cout << "  " << e.age  << '\n';
        else std::cout << "  Unknown column: " << query.selectedColumn << '\n';
    }
}

int main() {
    try {
        const std::vector<Employee> employees = {
            {"Pranav", 1, 19}, {"Aarav", 2, 20},  {"Karan", 3, 19},
            {"Sneha",         4, 21}, {"Vivaan", 5, 20}, {"Ishaan", 6, 31},
            {"Meera",         7, 22}, {"Devansh", 8, 33},
        };

        runQuery("SELECT name FROM employees WHERE id >= 3 OR age < 20",  employees);
        runQuery("SELECT name FROM employees WHERE id > 3 AND age >= 30", employees);
        runQuery("SELECT id   FROM employees WHERE (age < 25 AND id != 2) OR age >= 30", employees);
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}