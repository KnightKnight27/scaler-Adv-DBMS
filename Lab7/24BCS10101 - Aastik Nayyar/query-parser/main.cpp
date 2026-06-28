#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>

using namespace std;

struct Pupil {
    string fullName;
    int rollNo;
    int yearsOld;

    int getIntField(const string& field) const {
        if (field == "id")  return rollNo;
        if (field == "age") return yearsOld;

        throw runtime_error("Unknown numeric field: " + field);
    }

    string getStrField(const string& field) const {
        if (field == "name") return fullName;

        throw runtime_error("Unknown text field: " + field);
    }
};

enum class TokenType {
    KW_SELECT,
    KW_FROM,
    KW_WHERE,
    KW_OR,

    IDENTIFIER,
    NUMBER,

    OP_GT,
    OP_LT,
    OP_EQ,
    OP_GTE,
    OP_LTE,

    LPAREN,
    RPAREN,

    END_OF_INPUT
};

struct Token {
    TokenType kind;
    string value;
};

class Tokenizer {
private:
    string rawInput;

public:
    Tokenizer(const string& input) : rawInput(input) {}

    vector<Token> tokenize() {
        vector<Token> tokens;
        int pos = 0;

        while (pos < rawInput.size()) {

            if (isspace(rawInput[pos])) {
                pos++;
                continue;
            }

            if (isalpha(rawInput[pos])) {

                string word;

                while (pos < rawInput.size() &&
                       (isalnum(rawInput[pos]) ||
                        rawInput[pos] == '_')) {

                    word += rawInput[pos++];
                }

                string upper = word;
                transform(
                    upper.begin(),
                    upper.end(),
                    upper.begin(),
                    ::toupper
                );

                if (upper == "SELECT")
                    tokens.push_back({TokenType::KW_SELECT, word});
                else if (upper == "FROM")
                    tokens.push_back({TokenType::KW_FROM, word});
                else if (upper == "WHERE")
                    tokens.push_back({TokenType::KW_WHERE, word});
                else if (upper == "OR")
                    tokens.push_back({TokenType::KW_OR, word});
                else
                    tokens.push_back({TokenType::IDENTIFIER, word});

                continue;
            }

            if (isdigit(rawInput[pos])) {

                string numStr;

                while (pos < rawInput.size() &&
                       isdigit(rawInput[pos])) {

                    numStr += rawInput[pos++];
                }

                tokens.push_back({TokenType::NUMBER, numStr});
                continue;
            }

            if (rawInput[pos] == '>') {

                if (pos + 1 < rawInput.size() &&
                    rawInput[pos + 1] == '=') {

                    tokens.push_back({TokenType::OP_GTE, ">="});
                    pos += 2;
                }
                else {
                    tokens.push_back({TokenType::OP_GT, ">"});
                    pos++;
                }

                continue;
            }

            if (rawInput[pos] == '<') {

                if (pos + 1 < rawInput.size() &&
                    rawInput[pos + 1] == '=') {

                    tokens.push_back({TokenType::OP_LTE, "<="});
                    pos += 2;
                }
                else {
                    tokens.push_back({TokenType::OP_LT, "<"});
                    pos++;
                }

                continue;
            }

            if (rawInput[pos] == '=') {
                tokens.push_back({TokenType::OP_EQ, "="});
                pos++;
                continue;
            }

            if (rawInput[pos] == '(') {
                tokens.push_back({TokenType::LPAREN, "("});
                pos++;
                continue;
            }

            if (rawInput[pos] == ')') {
                tokens.push_back({TokenType::RPAREN, ")"});
                pos++;
                continue;
            }

            pos++;
        }

        tokens.push_back({TokenType::END_OF_INPUT, ""});
        return tokens;
    }
};

struct Node {
    virtual ~Node() = default;
};

struct LitNode : Node {
    int value;

    LitNode(int v)
        : value(v) {}
};

struct ColNode : Node {
    string colName;

    ColNode(const string& c)
        : colName(c) {}
};

struct BinNode : Node {
    string oper;
    Node* left;
    Node* right;

    BinNode(
        const string& op,
        Node* lhs,
        Node* rhs
    )
        : oper(op),
          left(lhs),
          right(rhs) {}
};

struct ParsedQuery {
    string selectCol;
    string fromTable;
    Node* condition;
};

class SQLParser {
private:
    vector<Token> tokens;
    int cursor = 0;

    Token consume(TokenType expected) {
        if (tokens[cursor].kind != expected)
            throw runtime_error("Unexpected token encountered");

        return tokens[cursor++];
    }

    Node* parseComparison() {

        string col =
            consume(TokenType::IDENTIFIER).value;

        Node* lhs =
            new ColNode(col);

        string op;

        if (tokens[cursor].kind == TokenType::OP_GTE) {
            op = ">=";
            consume(TokenType::OP_GTE);
        }
        else if (tokens[cursor].kind == TokenType::OP_LTE) {
            op = "<=";
            consume(TokenType::OP_LTE);
        }
        else if (tokens[cursor].kind == TokenType::OP_GT) {
            op = ">";
            consume(TokenType::OP_GT);
        }
        else if (tokens[cursor].kind == TokenType::OP_LT) {
            op = "<";
            consume(TokenType::OP_LT);
        }
        else {
            throw runtime_error(
                "Expected a comparison operator"
            );
        }

        int num =
            stoi(consume(TokenType::NUMBER).value);

        Node* rhs =
            new LitNode(num);

        return new BinNode(op, lhs, rhs);
    }

    Node* parsePrimary() {

        if (tokens[cursor].kind ==
            TokenType::LPAREN) {

            consume(TokenType::LPAREN);

            Node* inner =
                parseOr();

            consume(TokenType::RPAREN);

            return inner;
        }

        return parseComparison();
    }

    Node* parseOr() {

        Node* lhs =
            parsePrimary();

        while (tokens[cursor].kind ==
               TokenType::KW_OR) {

            consume(TokenType::KW_OR);

            Node* rhs =
                parsePrimary();

            lhs = new BinNode(
                "OR",
                lhs,
                rhs
            );
        }

        return lhs;
    }

public:
    SQLParser(const vector<Token>& toks)
        : tokens(toks) {}

    ParsedQuery parse() {

        consume(TokenType::KW_SELECT);

        string col =
            consume(TokenType::IDENTIFIER).value;

        consume(TokenType::KW_FROM);

        string tbl =
            consume(TokenType::IDENTIFIER).value;

        consume(TokenType::KW_WHERE);

        Node* cond =
            parseOr();

        return {
            col,
            tbl,
            cond
        };
    }
};

int evalInt(
    Node* node,
    const Pupil& row
) {

    if (auto* col =
            dynamic_cast<ColNode*>(node)) {

        return row.getIntField(
            col->colName
        );
    }

    if (auto* lit =
            dynamic_cast<LitNode*>(node)) {

        return lit->value;
    }

    throw runtime_error(
        "Cannot evaluate to integer"
    );
}

bool evalBool(
    Node* node,
    const Pupil& row
) {

    auto* bin =
        dynamic_cast<BinNode*>(node);

    if (!bin)
        throw runtime_error(
            "Malformed expression node"
        );

    if (bin->oper == "OR") {

        return evalBool(
                   bin->left,
                   row
               ) ||
               evalBool(
                   bin->right,
                   row
               );
    }

    int left =
        evalInt(
            bin->left,
            row
        );

    int right =
        evalInt(
            bin->right,
            row
        );

    if (bin->oper == ">")
        return left > right;

    if (bin->oper == "<")
        return left < right;

    if (bin->oper == ">=")
        return left >= right;

    if (bin->oper == "<=")
        return left <= right;

    if (bin->oper == "=")
        return left == right;

    throw runtime_error(
        "Unrecognized operator"
    );
}

void executeQuery(
    const ParsedQuery& query,
    const vector<Pupil>& dataset
) {

    for (const auto& row : dataset) {

        if (!evalBool(
                query.condition,
                row))
            continue;

        if (query.selectCol == "name")
            cout
                << row.getStrField("name")
                << endl;

        else if (query.selectCol == "id")
            cout
                << row.getIntField("id")
                << endl;

        else if (query.selectCol == "age")
            cout
                << row.getIntField("age")
                << endl;
    }
}

int main() {

    vector<Pupil> dataset = {
        {"Arjun", 1, 21},
        {"Priya", 2, 23},
        {"Vikram", 3, 27},
        {"Sneha", 4, 25},
        {"Rahul", 5, 21}
    };

    string sql =
        "SELECT name FROM students "
        "WHERE id <= 3";

    Tokenizer tokenizer(sql);

    vector<Token> tokens =
        tokenizer.tokenize();

    SQLParser parser(tokens);

    ParsedQuery query =
        parser.parse();

    executeQuery(
        query,
        dataset
    );

    return 0;
}
