// Lab 7 (Part 2) - Minimal SQL SELECT parser over in-memory rows
// Author: 24BCS10345 Ansh Mahajan
//
// A tiny query engine that runs a useful subset of SELECT against a vector of
// Row structs (id, name, age) held in memory. Supported grammar:
//
//   SELECT  <* | col[, col...]>
//   FROM    people
//   [WHERE  <cond> [AND <cond> ...]]
//   [ORDER BY <col> [ASC | DESC]]
//
//   cond := col <op> literal      op := =  !=  <  <=  >  >=
//
// Keywords are case-insensitive. String literals are single-quoted ('Riya');
// numeric columns (id, age) compare numerically, the name column compares
// lexicographically.
//
// Build: g++ -std=c++17 sql_parser.cpp -o parser
// Run:   ./parser

#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Row {
    int id;
    std::string name;
    int age;
};

// ---- the in-memory "table" -------------------------------------------------
const std::vector<Row>& people() {
    static const std::vector<Row> rows = {
        {101, "Ansh",   20},
        {102, "Riya",   22},
        {103, "Karan",  19},
        {104, "Sneha",  23},
        {105, "Vikram", 21},
    };
    return rows;
}

// ---- tokenizer -------------------------------------------------------------
struct Token {
    enum class Kind { Word, Number, String, Op, Comma, Star } kind;
    std::string text;
};

std::string upper(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

std::vector<Token> tokenize(const std::string& sql) {
    std::vector<Token> tokens;
    for (std::size_t i = 0; i < sql.size();) {
        char c = sql[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else if (c == ',') {
            tokens.push_back({Token::Kind::Comma, ","});
            ++i;
        } else if (c == '*') {
            tokens.push_back({Token::Kind::Star, "*"});
            ++i;
        } else if (c == '\'') {
            std::size_t j = i + 1;
            while (j < sql.size() && sql[j] != '\'') {
                ++j;
            }
            if (j >= sql.size()) {
                throw std::runtime_error("unterminated string literal");
            }
            tokens.push_back({Token::Kind::String, sql.substr(i + 1, j - i - 1)});
            i = j + 1;
        } else if (c == '=' || c == '<' || c == '>' || c == '!') {
            std::string op(1, c);
            if (i + 1 < sql.size() && sql[i + 1] == '=') {  // <= >= != ==
                op += '=';
                ++i;
            }
            tokens.push_back({Token::Kind::Op, op});
            ++i;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t j = i;
            while (j < sql.size() &&
                   std::isdigit(static_cast<unsigned char>(sql[j]))) {
                ++j;
            }
            tokens.push_back({Token::Kind::Number, sql.substr(i, j - i)});
            i = j;
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i;
            while (j < sql.size() &&
                   (std::isalnum(static_cast<unsigned char>(sql[j])) ||
                    sql[j] == '_')) {
                ++j;
            }
            tokens.push_back({Token::Kind::Word, sql.substr(i, j - i)});
            i = j;
        } else {
            throw std::runtime_error(std::string("illegal character '") + c + "'");
        }
    }
    return tokens;
}

// ---- a parsed condition ----------------------------------------------------
struct Condition {
    std::string column;   // id | name | age
    std::string op;       // = != < <= > >=
    std::string literal;  // raw value (numeric text or string)
};

bool compareInt(int lhs, const std::string& op, int rhs) {
    if (op == "=" || op == "==") return lhs == rhs;
    if (op == "!=") return lhs != rhs;
    if (op == "<")  return lhs < rhs;
    if (op == "<=") return lhs <= rhs;
    if (op == ">")  return lhs > rhs;
    if (op == ">=") return lhs >= rhs;
    throw std::runtime_error("bad operator " + op);
}

bool compareStr(const std::string& lhs, const std::string& op,
                const std::string& rhs) {
    if (op == "=" || op == "==") return lhs == rhs;
    if (op == "!=") return lhs != rhs;
    if (op == "<")  return lhs < rhs;
    if (op == "<=") return lhs <= rhs;
    if (op == ">")  return lhs > rhs;
    if (op == ">=") return lhs >= rhs;
    throw std::runtime_error("bad operator " + op);
}

bool rowMatches(const Row& r, const Condition& c) {
    if (c.column == "id")   return compareInt(r.id, c.op, std::stoi(c.literal));
    if (c.column == "age")  return compareInt(r.age, c.op, std::stoi(c.literal));
    if (c.column == "name") return compareStr(r.name, c.op, c.literal);
    throw std::runtime_error("unknown column in WHERE: " + c.column);
}

void validateColumn(const std::string& col) {
    if (col != "id" && col != "name" && col != "age") {
        throw std::runtime_error("unknown column: " + col);
    }
}

// ---- the parser + executor -------------------------------------------------
class QueryEngine {
public:
    void execute(const std::string& sql) {
        std::cout << "SQL> " << sql << '\n';
        try {
            run(tokenize(sql));
        } catch (const std::exception& e) {
            std::cout << "  error: " << e.what() << "\n\n";
        }
    }

private:
    std::vector<Token> toks_;
    std::size_t pos_ = 0;

    const Token& peek() const {
        static const Token end{Token::Kind::Word, ""};
        return pos_ < toks_.size() ? toks_[pos_] : end;
    }
    bool done() const { return pos_ >= toks_.size(); }
    Token next() { return toks_[pos_++]; }

    void expectWord(const std::string& kw) {
        if (done() || peek().kind != Token::Kind::Word ||
            upper(peek().text) != kw) {
            throw std::runtime_error("expected '" + kw + "'");
        }
        ++pos_;
    }

    void run(std::vector<Token> tokens) {
        toks_ = std::move(tokens);
        pos_ = 0;

        expectWord("SELECT");
        std::vector<std::string> columns = parseColumns();
        expectWord("FROM");
        expectWord("PEOPLE");                 // the only table we host

        std::vector<Condition> conditions;
        if (!done() && upper(peek().text) == "WHERE") {
            ++pos_;
            conditions = parseConditions();
        }

        std::string orderCol;
        bool descending = false;
        if (!done() && upper(peek().text) == "ORDER") {
            ++pos_;
            expectWord("BY");
            orderCol = parseColumnName();
            if (!done() && upper(peek().text) == "ASC") {
                ++pos_;
            } else if (!done() && upper(peek().text) == "DESC") {
                descending = true;
                ++pos_;
            }
        }
        if (!done()) {
            throw std::runtime_error("unexpected token '" + peek().text + "'");
        }

        // ---- execute ----
        std::vector<Row> result;
        for (const Row& r : people()) {
            bool keep = true;
            for (const Condition& c : conditions) {
                if (!rowMatches(r, c)) {
                    keep = false;
                    break;
                }
            }
            if (keep) {
                result.push_back(r);
            }
        }
        if (!orderCol.empty()) {
            sortRows(result, orderCol, descending);
        }
        printResult(columns, result);
    }

    std::vector<std::string> parseColumns() {
        std::vector<std::string> columns;
        if (peek().kind == Token::Kind::Star) {
            ++pos_;
            return {"id", "name", "age"};     // expand * to all columns
        }
        while (true) {
            columns.push_back(parseColumnName());
            if (!done() && peek().kind == Token::Kind::Comma) {
                ++pos_;
                continue;
            }
            break;
        }
        return columns;
    }

    std::string parseColumnName() {
        if (done() || peek().kind != Token::Kind::Word) {
            throw std::runtime_error("expected a column name");
        }
        std::string col = next().text;
        validateColumn(col);
        return col;
    }

    std::vector<Condition> parseConditions() {
        std::vector<Condition> conditions;
        while (true) {
            Condition c;
            c.column = parseColumnName();
            if (done() || peek().kind != Token::Kind::Op) {
                throw std::runtime_error("expected a comparison operator");
            }
            c.op = next().text;
            if (done()) {
                throw std::runtime_error("expected a value after operator");
            }
            Token v = next();
            if (v.kind != Token::Kind::Number && v.kind != Token::Kind::String) {
                throw std::runtime_error("expected a literal value");
            }
            c.literal = v.text;
            conditions.push_back(c);

            if (!done() && upper(peek().text) == "AND") {
                ++pos_;
                continue;
            }
            break;
        }
        return conditions;
    }

    static void sortRows(std::vector<Row>& rows, const std::string& col,
                         bool descending) {
        std::stable_sort(rows.begin(), rows.end(),
                         [&](const Row& a, const Row& b) {
                             bool less;
                             if (col == "id")        less = a.id < b.id;
                             else if (col == "age")  less = a.age < b.age;
                             else                    less = a.name < b.name;
                             return descending ? !less : less;
                         });
    }

    static void printResult(const std::vector<std::string>& columns,
                            const std::vector<Row>& rows) {
        for (std::size_t i = 0; i < columns.size(); ++i) {
            std::cout << "  " << columns[i] << (i + 1 < columns.size() ? " |" : "");
        }
        std::cout << '\n';
        for (const Row& r : rows) {
            std::cout << "  ";
            for (std::size_t i = 0; i < columns.size(); ++i) {
                if (columns[i] == "id")        std::cout << r.id;
                else if (columns[i] == "name") std::cout << r.name;
                else                           std::cout << r.age;
                if (i + 1 < columns.size()) {
                    std::cout << " | ";
                }
            }
            std::cout << '\n';
        }
        std::cout << "  (" << rows.size() << " row(s))\n\n";
    }
};

}  // namespace

int main() {
    QueryEngine engine;

    const std::vector<std::string> queries = {
        "SELECT * FROM people",
        "SELECT name, age FROM people WHERE age >= 21",
        "SELECT name FROM people WHERE name = 'Riya'",
        "SELECT * FROM people WHERE age > 20 ORDER BY age DESC",
        "SELECT id, name FROM people WHERE age >= 20 AND id < 104",
        "select id, name from people order by name asc",   // case-insensitive
        "SELECT salary FROM people",                        // unknown column
    };

    std::cout << "=== Minimal SQL SELECT engine over in-memory rows ===\n\n";
    for (const std::string& q : queries) {
        engine.execute(q);
    }

    // Extra queries can be piped in, one per line.
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty()) {
            engine.execute(line);
        }
    }
    return 0;
}
