// Lab 7 — SQL WHERE evaluation via a recursive-descent parser.
//
// Name : Shaurya Verma
// Roll : 24BCS10151
//
// Same query as the shunting-yard version —
//
//     SELECT name FROM students WHERE marks >= 80 AND (age < 20 OR id = 5)
//
// but here a handwritten lexer feeds three mutually-recursive functions
// that build an abstract syntax tree (AST). Precedence is not a table;
// it is baked into the order parseOr -> parseAnd -> parseCompare calls
// itself. We then walk the tree to evaluate each row.

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <cctype>

namespace {

struct Row {
    int id;
    std::string name;
    int age;
    int marks;
};

// ---------------------------------------------------------------- lexer

enum class Type { Ident, Number, Op, LParen, RParen, End };

struct Token {
    Type type;
    std::string text;
    int value = 0;
};

std::vector<Token> lex(const std::string& src) {
    std::vector<Token> out;
    size_t i = 0;
    while (i < src.size()) {
        char c = src[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else if (c == '(') {
            out.push_back({Type::LParen, "(", 0}); ++i;
        } else if (c == ')') {
            out.push_back({Type::RParen, ")", 0}); ++i;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t s = i;
            while (i < src.size() &&
                   std::isdigit(static_cast<unsigned char>(src[i]))) ++i;
            std::string num = src.substr(s, i - s);
            out.push_back({Type::Number, num, std::stoi(num)});
        } else if (c == '<' || c == '>' || c == '=') {
            // Greedily grab a two-char comparison (<=, >=) if present.
            std::string op(1, c);
            ++i;
            if (i < src.size() && src[i] == '=' && (c == '<' || c == '>')) {
                op += '='; ++i;
            }
            out.push_back({Type::Op, op, 0});
        } else if (std::isalpha(static_cast<unsigned char>(c))) {
            size_t s = i;
            while (i < src.size() &&
                   std::isalnum(static_cast<unsigned char>(src[i]))) ++i;
            std::string word = src.substr(s, i - s);
            // AND / OR are operators; everything else is a column name.
            if (word == "AND" || word == "OR")
                out.push_back({Type::Op, word, 0});
            else
                out.push_back({Type::Ident, word, 0});
        } else {
            ++i;   // skip anything unexpected
        }
    }
    out.push_back({Type::End, "", 0});
    return out;
}

// ------------------------------------------------------------------ AST

// A node is either a comparison leaf (op + column + literal) or a
// boolean interior node (AND/OR with two children).
struct Node {
    std::string op;                  // "AND", "OR", "=", "<", ...
    std::string column;              // set on comparison leaves
    int literal = 0;                 // set on comparison leaves
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;

    bool is_leaf() const { return left == nullptr && right == nullptr; }
};

// Recursive-descent parser. `pos` is the cursor into the token stream;
// the grammar is:
//   parseOr      -> parseAnd ( OR parseAnd )*
//   parseAnd     -> parseCompare ( AND parseCompare )*
//   parseCompare -> '(' parseOr ')' | Ident Op Number
class Parser {
public:
    explicit Parser(std::vector<Token> tokens)
        : toks_(std::move(tokens)) {}

    std::unique_ptr<Node> parse() { return parseOr(); }

private:
    const Token& peek() const { return toks_[pos_]; }
    const Token& next()       { return toks_[pos_++]; }

    std::unique_ptr<Node> parseOr() {
        auto node = parseAnd();
        while (peek().type == Type::Op && peek().text == "OR") {
            next();
            auto rhs = parseAnd();
            auto combined = std::make_unique<Node>();
            combined->op = "OR";
            combined->left = std::move(node);
            combined->right = std::move(rhs);
            node = std::move(combined);
        }
        return node;
    }

    std::unique_ptr<Node> parseAnd() {
        auto node = parseCompare();
        while (peek().type == Type::Op && peek().text == "AND") {
            next();
            auto rhs = parseCompare();
            auto combined = std::make_unique<Node>();
            combined->op = "AND";
            combined->left = std::move(node);
            combined->right = std::move(rhs);
            node = std::move(combined);
        }
        return node;
    }

    std::unique_ptr<Node> parseCompare() {
        if (peek().type == Type::LParen) {
            next();                     // consume '('
            auto inner = parseOr();
            if (peek().type == Type::RParen) next();   // consume ')'
            return inner;
        }
        // Ident Op Number
        auto leaf = std::make_unique<Node>();
        leaf->column = next().text;     // Ident
        leaf->op     = next().text;     // comparison operator
        leaf->literal = next().value;   // Number
        return leaf;
    }

    std::vector<Token> toks_;
    size_t pos_ = 0;
};

// ------------------------------------------------------------ evaluation

int column_value(const std::string& col, const Row& r) {
    if (col == "id")    return r.id;
    if (col == "age")   return r.age;
    if (col == "marks") return r.marks;
    return 0;
}

bool evaluate(const Node* n, const Row& r) {
    if (n->is_leaf()) {
        int lhs = column_value(n->column, r);
        int rhs = n->literal;
        const std::string& op = n->op;
        if (op == "=")  return lhs == rhs;
        if (op == "<")  return lhs <  rhs;
        if (op == ">")  return lhs >  rhs;
        if (op == "<=") return lhs <= rhs;
        if (op == ">=") return lhs >= rhs;
        return false;
    }
    if (n->op == "AND") return evaluate(n->left.get(), r) &&
                               evaluate(n->right.get(), r);
    return evaluate(n->left.get(), r) || evaluate(n->right.get(), r);
}

void print_tree(const Node* n, int depth) {
    std::string pad(static_cast<size_t>(depth) * 2, ' ');
    if (n->is_leaf()) {
        std::cout << pad << n->op << "\n";
        std::cout << pad << "  " << n->column << "\n";
        std::cout << pad << "  " << n->literal << "\n";
        return;
    }
    std::cout << pad << n->op << "\n";
    print_tree(n->left.get(),  depth + 1);
    print_tree(n->right.get(), depth + 1);
}

}  // namespace

int main() {
    const std::vector<Row> students = {
        {1, "Priya", 19, 88},
        {2, "Rohan", 22, 67},
        {3, "Sneha", 20, 91},
        {4, "Arjun", 23, 74},
        {5, "Meera", 21, 95},
        {6, "Karan", 18, 59},
    };

    const std::string query =
        "SELECT name FROM students WHERE marks >= 80 AND (age < 20 OR id = 5)";
    const std::string where = "marks >= 80 AND (age < 20 OR id = 5)";

    Parser parser(lex(where));
    std::unique_ptr<Node> ast = parser.parse();

    std::cout << "Lab 7 - recursive-descent parser (Shaurya Verma, 24BCS10151)\n\n";
    std::cout << "Query: " << query << "\n\n";
    std::cout << "WHERE as an AST (precedence encoded in tree shape):\n";
    print_tree(ast.get(), 0);

    std::cout << "\nMatching rows (SELECT name FROM students):\n";
    for (const Row& r : students) {
        if (evaluate(ast.get(), r))
            std::cout << "  " << r.name << "\n";
    }
    return 0;
}
