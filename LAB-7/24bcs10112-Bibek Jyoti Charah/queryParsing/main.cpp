// Lab 7 - Recursive-descent SQL SELECT parser + evaluator
// Bibek Jyoti Charah (24bcs10112)
//
// Parses  SELECT <col> FROM <table> WHERE <expr>  into an AST and walks it
// per row. The grammar bakes precedence in (OR < AND < comparison), so no
// shunting yard is needed on this side:
//
//   expr       := orTerm
//   orTerm     := andTerm ( OR  andTerm )*
//   andTerm    := factor  ( AND factor  )*
//   factor     := '(' expr ')' | comparison
//   comparison := <ident> <op> <number>

#include <cctype>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct Row {
    std::string name;
    int id;
    int age;
};

struct Expr {
    std::string op;                 // comparison op, or "AND" / "OR"
    std::string col;                // set on comparison leaves
    int val = 0;
    std::unique_ptr<Expr> lhs, rhs;
};

std::vector<std::string> lex(const std::string &q) {
    std::vector<std::string> tokens;
    for (std::size_t i = 0; i < q.size();) {
        char c = q[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i;
            while (j < q.size() && (std::isalnum(static_cast<unsigned char>(q[j])) || q[j] == '_')) ++j;
            tokens.push_back(q.substr(i, j - i));
            i = j;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t j = i;
            while (j < q.size() && std::isdigit(static_cast<unsigned char>(q[j]))) ++j;
            tokens.push_back(q.substr(i, j - i));
            i = j;
        } else if ((c == '<' || c == '>' || c == '!') && i + 1 < q.size() && q[i + 1] == '=') {
            tokens.push_back(q.substr(i, 2));
            i += 2;
        } else {
            tokens.push_back(std::string(1, c));
            ++i;
        }
    }
    return tokens;
}

class Parser {
public:
    struct Query {
        std::string col, table;
        std::unique_ptr<Expr> where;
    };

    explicit Parser(const std::vector<std::string> &tokens) : tk(tokens) {}

    Query parse() {
        Query q;
        expect("SELECT");
        q.col = next();
        expect("FROM");
        q.table = next();
        expect("WHERE");
        q.where = orTerm();
        if (pos != tk.size()) throw std::runtime_error("trailing tokens after WHERE");
        return q;
    }

private:
    const std::vector<std::string> &tk;
    std::size_t pos = 0;

    static std::string upper(std::string s) {
        for (char &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }
    const std::string &peek() const {
        if (pos >= tk.size()) throw std::runtime_error("unexpected end of input");
        return tk[pos];
    }
    std::string next() {
        if (pos >= tk.size()) throw std::runtime_error("unexpected end of input");
        return tk[pos++];
    }
    bool keyword(const std::string &kw) const { return pos < tk.size() && upper(tk[pos]) == kw; }
    void expect(const std::string &kw) {
        if (upper(next()) != kw) throw std::runtime_error("expected " + kw);
    }

    std::unique_ptr<Expr> orTerm() {
        auto node = andTerm();
        while (keyword("OR")) {
            ++pos;
            node = join("OR", std::move(node), andTerm());
        }
        return node;
    }
    std::unique_ptr<Expr> andTerm() {
        auto node = factor();
        while (keyword("AND")) {
            ++pos;
            node = join("AND", std::move(node), factor());
        }
        return node;
    }
    std::unique_ptr<Expr> factor() {
        if (peek() == "(") {
            ++pos;
            auto inner = orTerm();
            if (next() != ")") throw std::runtime_error("expected ')'");
            return inner;
        }
        auto leaf = std::make_unique<Expr>();
        leaf->col = next();
        leaf->op = next();
        if (leaf->op != ">" && leaf->op != "<" && leaf->op != ">=" &&
            leaf->op != "<=" && leaf->op != "=" && leaf->op != "!=")
            throw std::runtime_error("bad comparison operator: " + leaf->op);
        leaf->val = std::stoi(next());
        return leaf;
    }
    static std::unique_ptr<Expr> join(const std::string &op,
                                      std::unique_ptr<Expr> l,
                                      std::unique_ptr<Expr> r) {
        auto node = std::make_unique<Expr>();
        node->op = op;
        node->lhs = std::move(l);
        node->rhs = std::move(r);
        return node;
    }
};

bool eval(const Expr *e, const Row &r) {
    if (e->op == "AND") return eval(e->lhs.get(), r) && eval(e->rhs.get(), r);
    if (e->op == "OR")  return eval(e->lhs.get(), r) || eval(e->rhs.get(), r);

    int v = e->col == "id" ? r.id : e->col == "age" ? r.age : -1;
    if (e->col != "id" && e->col != "age") throw std::runtime_error("unknown column: " + e->col);

    if (e->op == ">")  return v > e->val;
    if (e->op == "<")  return v < e->val;
    if (e->op == ">=") return v >= e->val;
    if (e->op == "<=") return v <= e->val;
    if (e->op == "=")  return v == e->val;
    return v != e->val;
}

void run(const std::string &sql, const std::vector<Row> &rows) {
    auto tokens = lex(sql);
    Parser parser(tokens);
    auto query = parser.parse();

    std::cout << "\n" << sql << '\n';
    for (const auto &r : rows) {
        if (!eval(query.where.get(), r)) continue;
        if (query.col == "name")      std::cout << "  " << r.name << '\n';
        else if (query.col == "id")   std::cout << "  " << r.id << '\n';
        else if (query.col == "age")  std::cout << "  " << r.age << '\n';
        else                          std::cout << "  (unknown column " << query.col << ")\n";
    }
}

int main() {
    const std::vector<Row> rows = {
        {"Asha", 1, 19}, {"Rohan", 2, 20}, {"Kabir", 3, 19}, {"Sara", 4, 21},
        {"Vivaan", 5, 20}, {"Ira", 6, 31}, {"Neel", 7, 22}, {"Diya", 8, 33},
    };

    try {
        run("SELECT name FROM employees WHERE id >= 3 OR age < 20", rows);
        run("SELECT name FROM employees WHERE id > 3 AND age >= 30", rows);
        run("SELECT id FROM employees WHERE (age < 25 AND id != 2) OR age >= 30", rows);
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
